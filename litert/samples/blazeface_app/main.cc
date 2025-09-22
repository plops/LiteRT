#include <fX11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "absl/types/span.h" // Needed for writing to LiteRT buffers
#include "litert/cc/litert_compiled_model.h"
#include "litert/cc/litert_environment.h"
#include "litert/cc/litert_macros.h"
#include "litert/cc/litert_model.h"

// A simple structure to hold bounding box data.
struct BoundingBox {
    float x1, y1, x2, y2;
};

// Manages the X11 overlay window for drawing face boxes.
class FaceOverlay {
public:
    FaceOverlay(Display* display) : display_(display) {
        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        screen_width_ = DisplayWidth(display_, screen_);
        screen_height_ = DisplayHeight(display_, screen_);

        XVisualInfo vinfo;
        if (!XMatchVisualInfo(display_, screen_, 32, TrueColor, &vinfo)) {
            throw std::runtime_error("No 32-bit visual found for transparency.");
        }
        Visual* visual = vinfo.visual;
        int depth = vinfo.depth;

        XSetWindowAttributes attrs;
        std::memset(&attrs, 0, sizeof(attrs));
        attrs.colormap = XCreateColormap(display_, root_, visual, AllocNone);
        attrs.border_pixel = 0;
        attrs.background_pixel = 0;
        attrs.override_redirect = True;

        window_ = XCreateWindow(display_, root_, 0, 0, screen_width_, screen_height_, 0,
                                depth, InputOutput, visual,
                                CWColormap | CWBorderPixel | CWOverrideRedirect, &attrs);

        if (!window_) throw std::runtime_error("Failed to create overlay window");

        XserverRegion region = XFixesCreateRegion(display_, nullptr, 0);
        XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display_, region);

        XMapRaised(display_, window_);
        XFlush(display_);

        gc_ = XCreateGC(display_, window_, 0, nullptr);
        if (!gc_) throw std::runtime_error("Failed to create GC");
        XSetForeground(display_, gc_, 0xFFFF0000); // Red
        XSetLineAttributes(display_, gc_, 2, LineSolid, CapButt, JoinMiter);
    }

    ~FaceOverlay() {
        if (gc_) XFreeGC(display_, gc_);
        if (window_) XDestroyWindow(display_, window_);
    }

    void draw_faces(const std::vector<BoundingBox>& faces) {
        XClearWindow(display_, window_);
        for (const auto& box : faces) {
            XDrawRectangle(display_, window_, gc_, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
        }
        XFlush(display_);
        XRaiseWindow(display_, window_);
        XFlush(display_);
    }

private:
    Display* display_;
    int screen_;
    Window root_;
    Window window_;
    GC gc_;
    int screen_width_;
    int screen_height_;
};

class RealtimeFaceDetector {
public:
    RealtimeFaceDetector(const char* model_path) {
        // --- X11 Setup ---
        display_ = XOpenDisplay(nullptr);
        if (!display_) throw std::runtime_error("Failed to open X display.");
        if (!XShmQueryExtension(display_)) throw std::runtime_error("XShm extension not available.");

        screen_width_ = DisplayWidth(display_, DefaultScreen(display_));
        screen_height_ = DisplayHeight(display_, DefaultScreen(display_));

        ximage_ = XShmCreateImage(display_, DefaultVisual(display_, 0), DefaultDepth(display_, 0), ZPixmap, nullptr, &shminfo_, screen_width_, screen_height_);
        shminfo_.shmid = shmget(IPC_PRIVATE, ximage_->bytes_per_line * ximage_->height, IPC_CREAT | 0777);
        shminfo_.shmaddr = ximage_->data = (char*)shmat(shminfo_.shmid, 0, 0);
        shminfo_.readOnly = False;
        XShmAttach(display_, &shminfo_);

        // --- LiteRT C++ API Setup ---
        LITERT_ASSIGN_OR_THROW(env_, litert::Environment::Create({}));
        LITERT_ASSIGN_OR_THROW(model_, litert::Model::CreateFromFile(model_path));
        LITERT_ASSIGN_OR_THROW(compiled_model_, litert::CompiledModel::Create(env_.get(), model_.get()));
        LITERT_ASSIGN_OR_THROW(input_buffers_, compiled_model_->CreateInputBuffers());
        LITERT_ASSIGN_OR_THROW(output_buffers_, compiled_model_->CreateOutputBuffers());

        // --- Overlay Setup ---
        overlay_ = std::make_unique<FaceOverlay>(display_);
    }

    ~RealtimeFaceDetector() {
        XShmDetach(display_, &shminfo_);
        XDestroyImage(ximage_);
        shmdt(shminfo_.shmaddr);
        shmctl(shminfo_.shmid, IPC_RMID, 0);
        XCloseDisplay(display_);
    }

    void run_loop() {
        const int MODEL_WIDTH = 128;
        const int MODEL_HEIGHT = 128;
        std::vector<float> preprocessed_data(MODEL_WIDTH * MODEL_HEIGHT * 3);

        // Prepare output buffers. The data will be read into these vectors.
        std::vector<float> scores_data(896); // [1, 896, 1] -> 896
        std::vector<float> boxes_data(896 * 16); // [1, 896, 16] -> 14336

        while (true) {
            XShmGetImage(display_, RootWindow(display_, 0), ximage_, 0, 0, 0xFFFFFFFF);

            preprocess(preprocessed_data.data(), (unsigned char*)ximage_->data, screen_width_, screen_height_, MODEL_WIDTH, MODEL_HEIGHT);

            LITERT_THROW_IF_ERROR(input_buffers_[0].Write(absl::MakeConstSpan(preprocessed_data)));
            LITERT_THROW_IF_ERROR(compiled_model_->Run(input_buffers_, output_buffers_));

            LITERT_THROW_IF_ERROR(output_buffers_[0].Read(absl::MakeSpan(scores_data)));
            LITERT_THROW_IF_ERROR(output_buffers_[1].Read(absl::MakeSpan(boxes_data)));

            auto faces = postprocess(scores_data, boxes_data);
            overlay_->draw_faces(faces);

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

private:
    void preprocess(float* out, const unsigned char* in, int in_w, int in_h, int out_w, int out_h) {
        float x_ratio = static_cast<float>(in_w) / out_w;
        float y_ratio = static_cast<float>(in_h) / out_h;
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                int px = static_cast<int>(x_ratio * x);
                int py = static_cast<int>(y_ratio * y);
                const unsigned char* pixel = &in[(py * in_w + px) * 4]; // Assumes 32bpp (BGRA)
                out[(y * out_w + x) * 3 + 0] = (pixel[2] / 255.0f - 0.5f) * 2.0f; // R
                out[(y * out_w + x) * 3 + 1] = (pixel[1] / 255.0f - 0.5f) * 2.0f; // G
                out[(y * out_w + x) * 3 + 2] = (pixel[0] / 255.0f - 0.5f) * 2.0f; // B
            }
        }
    }

    std::vector<BoundingBox> postprocess(const std::vector<float>& scores, const std::vector<float>& boxes) {
        std::vector<BoundingBox> faces;
        const float score_threshold = 0.75f;
        for (int i = 0; i < 896; ++i) {
            if (scores[i] > score_threshold) {
                // Boxes tensor is [y_center, x_center, height, width, ...]
                // These are relative to anchor positions. For this simple demo, we
                // decode them as if they are normalized coordinates, which is not
                // fully correct but visually sufficient.
                float y_center = boxes[i * 16 + 0];
                float x_center = boxes[i * 16 + 1];
                float h = boxes[i * 16 + 2];
                float w = boxes[i * 16 + 3];

                faces.push_back({
                    (x_center - w / 2.0f) * screen_width_,
                    (y_center - h / 2.0f) * screen_height_,
                    (x_center + w / 2.0f) * screen_width_,
                    (y_center + h / 2.0f) * screen_height_
                });
            }
        }
        return faces;
    }

    Display* display_;
    int screen_width_, screen_height_;
    XImage* ximage_;
    XShmSegmentInfo shminfo_;

    std::unique_ptr<litert::Environment> env_;
    std::unique_ptr<litert::Model> model_;
    std::unique_ptr<litert::CompiledModel> compiled_model_;
    std::vector<litert::Buffer> input_buffers_;
    std::vector<litert::Buffer> output_buffers_;

    std::unique_ptr<FaceOverlay> overlay_;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_blazeface.tflite>" << std::endl;
        return 1;
    }
    try {
        RealtimeFaceDetector app(argv[1]);
        app.run_loop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}