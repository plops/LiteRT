#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

// CORRECTED INCLUDE PATHS
#include "c_api.h"
#include "c_api_types.h"

// A simple structure to hold bounding box data.
struct BoundingBox {
    int x1, y1, x2, y2;
};

// Manages the X11 overlay window for drawing face boxes.
class FaceOverlay {
public:
    FaceOverlay(Display* display) : display_(display) {
        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        screen_width_ = DisplayWidth(display_, screen_);
        screen_height_ = DisplayHeight(display_, screen_);

        // Find a 32-bit visual for transparency (ARGB).
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
        attrs.background_pixel = 0; // Transparent background
        attrs.override_redirect = True;

        window_ = XCreateWindow(display_, root_, 0, 0, screen_width_, screen_height_, 0,
                                depth, InputOutput, visual,
                                CWColormap | CWBorderPixel | CWOverrideRedirect, &attrs);

        if (!window_) throw std::runtime_error("Failed to create overlay window");

        // Make the window click-through.
        XserverRegion region = XFixesCreateRegion(display_, nullptr, 0);
        XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display_, region);

        XMapRaised(display_, window_);
        XFlush(display_);

        // Create a Graphics Context for drawing red rectangles.
        gc_ = XCreateGC(display_, window_, 0, nullptr);
        if (!gc_) throw std::runtime_error("Failed to create GC");
        XSetForeground(display_, gc_, 0xFFFF0000); // Red color (AARRGGBB)
        XSetLineAttributes(display_, gc_, 3, LineSolid, CapButt, JoinMiter);
    }

    ~FaceOverlay() {
        if (gc_) XFreeGC(display_, gc_);
        if (window_) XDestroyWindow(display_, window_);
        // Display is owned by the main function, not closed here.
    }

    void draw_faces(const std::vector<BoundingBox>& faces) {
        XClearWindow(display_, window_);
        for (const auto& box : faces) {
            XDrawRectangle(display_, window_, gc_, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
        }
        XFlush(display_);
        XRaiseWindow(display_, window_); // Keep it on top
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

// Main application class to orchestrate capture, detection, and drawing.
class RealtimeFaceDetector {
public:
    RealtimeFaceDetector(const char* model_path) {
        // --- X11 and Screen Capture Setup ---
        display_ = XOpenDisplay(nullptr);
        if (!display_) throw std::runtime_error("Failed to open X display.");
        if (!XShmQueryExtension(display_)) throw std::runtime_error("XShm extension not available.");

        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        screen_width_ = DisplayWidth(display_, screen_);
        screen_height_ = DisplayHeight(display_, screen_);

        // Create XShm image
        ximage_ = XShmCreateImage(display_, DefaultVisual(display_, screen_), DefaultDepth(display_, screen_), ZPixmap, nullptr, &shminfo_, screen_width_, screen_height_);
        shminfo_.shmid = shmget(IPC_PRIVATE, ximage_->bytes_per_line * ximage_->height, IPC_CREAT | 0777);
        shminfo_.shmaddr = ximage_->data = (char*)shmat(shminfo_.shmid, 0, 0);
        shminfo_.readOnly = False;
        XShmAttach(display_, &shminfo_);

        // --- LiteRT Setup ---
        env_ = LiteRtEnvCreateWithOptions(LiteRtEnvOptionsCreate());
        runtime_ = LiteRtRuntimeCreate(env_, LiteRtRuntimeOptionsCreate());
        model_ = LiteRtModelCreateFromFile(model_path);
        if (!model_) throw std::runtime_error("Failed to load model.");
        interpreter_ = LiteRtInterpreterCreate(runtime_, model_, LiteRtInterpreterOptionsCreate());
        if (!interpreter_) throw std::runtime_error("Failed to create interpreter.");

        // --- Overlay Setup ---
        overlay_ = std::make_unique<FaceOverlay>(display_);
    }

    ~RealtimeFaceDetector() {
        LiteRtInterpreterDelete(interpreter_);
        LiteRtModelDelete(model_);
        LiteRtRuntimeDelete(runtime_);
        LiteRtEnvDelete(env_);

        XShmDetach(display_, &shminfo_);
        XDestroyImage(ximage_);
        shmdt(shminfo_.shmaddr);
        shmctl(shminfo_.shmid, IPC_RMID, 0);
        XCloseDisplay(display_);
    }

    void run_loop() {
        const int MODEL_WIDTH = 128;
        const int MODEL_HEIGHT = 128;
        std::vector<float> input_tensor_data(MODEL_WIDTH * MODEL_HEIGHT * 3);

        while (true) {
            // 1. Capture the screen
            XShmGetImage(display_, root_, ximage_, 0, 0, 0xFFFFFFFF);

            // 2. Preprocess: Resize and normalize the image
            preprocess(input_tensor_data.data(), (unsigned char*)ximage_->data, screen_width_, screen_height_, MODEL_WIDTH, MODEL_HEIGHT);

            // 3. Run Inference
            std::vector<int> input_dims = {1, MODEL_HEIGHT, MODEL_WIDTH, 3};
            LiteRtTensor* input_tensor = LiteRtTensorCreate(
                kLiteRtFloat32, input_dims.data(), input_dims.size(),
                input_tensor_data.data(), input_tensor_data.size() * sizeof(float));

            std::vector<LiteRtTensor*> inputs = {input_tensor};
            if (LiteRtInterpreterInvoke(interpreter_, inputs.data(), inputs.size()) != kLiteRtOk) {
                std::cerr << "Inference failed." << std::endl;
                continue;
            }
            LiteRtTensorDelete(input_tensor);

            // 4. Post-process the results
            const LiteRtTensor* boxes_tensor = LiteRtInterpreterGetOutputTensor(interpreter_, 1);
            const LiteRtTensor* scores_tensor = LiteRtInterpreterGetOutputTensor(interpreter_, 0);

            auto faces = postprocess(
                static_cast<const float*>(LiteRtTensorGetData(boxes_tensor)),
                static_cast<const float*>(LiteRtTensorGetData(scores_tensor))
            );

            // 5. Draw the bounding boxes on the overlay
            overlay_->draw_faces(faces);

            // Limit the frame rate
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 FPS
        }
    }

private:
    Display* display_;
    int screen_;
    Window root_;
    int screen_width_;
    int screen_height_;
    XImage* ximage_;
    XShmSegmentInfo shminfo_;

    LiteRtEnv* env_ = nullptr;
    LiteRtRuntime* runtime_ = nullptr;
    LiteRtModel* model_ = nullptr;
    LiteRtInterpreter* interpreter_ = nullptr;

    std::unique_ptr<FaceOverlay> overlay_;

    // Simple nearest-neighbor resize and normalization.
    void preprocess(float* out, const unsigned char* in, int in_w, int in_h, int out_w, int out_h) {
        float x_ratio = in_w / (float)out_w;
        float y_ratio = in_h / (float)out_h;
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                int px = x_ratio * x;
                int py = y_ratio * y;
                // Assuming 32bpp BGRA format from X11
                const unsigned char* pixel = &in[(py * in_w + px) * 4];
                // Normalize to [-1, 1] as expected by BlazeFace
                out[(y * out_w + x) * 3 + 0] = (pixel[2] / 255.0f - 0.5f) * 2.0f; // R
                out[(y * out_w + x) * 3 + 1] = (pixel[1] / 255.0f - 0.5f) * 2.0f; // G
                out[(y * out_w + x) * 3 + 2] = (pixel[0] / 255.0f - 0.5f) * 2.0f; // B
            }
        }
    }

    // Decode model output to bounding boxes.
    std::vector<BoundingBox> postprocess(const float* boxes, const float* scores) {
        std::vector<BoundingBox> faces;
        const float score_threshold = 0.75f;
        const int num_boxes = 896; // BlazeFace has 896 anchor boxes

        for (int i = 0; i < num_boxes; ++i) {
            if (scores[i] > score_threshold) {
                // The boxes tensor contains [y_center, x_center, height, width, ...]
                // This is a simplified decoding that assumes the model outputs direct coordinates.
                // A full implementation requires using pre-defined anchor boxes.
                float y_center = boxes[i * 16 + 0];
                float x_center = boxes[i * 16 + 1];
                float h = boxes[i * 16 + 2];
                float w = boxes[i * 16 + 3];

                BoundingBox box;
                box.x1 = (x_center - w / 2.0f) * screen_width_;
                box.y1 = (y_center - h / 2.0f) * screen_height_;
                box.x2 = (x_center + w / 2.0f) * screen_width_;
                box.y2 = (y_center + h / 2.0f) * screen_height_;

                faces.push_back(box);
            }
        }
        return faces;
    }
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