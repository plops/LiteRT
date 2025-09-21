// litert/samples/blazeface_app/main.cc

#include <iostream>
#include <vector>
#include <fstream>
#include <memory>

#include "litert/c/c_api.h"
#include "litert/c/c_api_types.h"

// For a real application, you would use a library like OpenCV to load and
// preprocess the image. This example will simulate a preprocessed image.

void PrintTensorDetails(const LiteRtTensor* tensor, const std::string& name) {
    std::cout << "Tensor Name: " << name << std::endl;
    std::cout << "  - Type: " << LiteRtTensorGetType(tensor) << std::endl;
    std::cout << "  - Dimensions: ";
    int num_dims = LiteRtTensorGetNumDims(tensor);
    const int* dims = LiteRtTensorGetDims(tensor);
    for (int i = 0; i < num_dims; ++i) {
        std::cout << dims[i] << (i == num_dims - 1 ? "" : ", ");
    }
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_blazeface.tflite>" << std::endl;
        return 1;
    }
    const char* model_path = argv[1];

    // 1. Initialize LiteRT Environment and Runtime
    std::cout << "Initializing LiteRT..." << std::endl;
    LiteRtEnvOptions* env_options = LiteRtEnvOptionsCreate();
    LiteRtEnv* env = LiteRtEnvCreateWithOptions(env_options);
    LiteRtRuntimeOptions* runtime_options = LiteRtRuntimeOptionsCreate();
    LiteRtRuntime* runtime = LiteRtRuntimeCreate(env, runtime_options);
    LiteRtModel* model = LiteRtModelCreateFromFile(model_path);
    if (!model) {
        std::cerr << "Failed to load model from " << model_path << std::endl;
        return 1;
    }

    // 2. Create Interpreter
    LiteRtInterpreterOptions* interpreter_options = LiteRtInterpreterOptionsCreate();
    LiteRtInterpreter* interpreter = LiteRtInterpreterCreate(runtime, model, interpreter_options);
    if (!interpreter) {
        std::cerr << "Failed to create interpreter." << std::endl;
        return 1;
    }

    // 3. Prepare Input Tensor
    // BlazeFace typically takes a 1x128x128x3 float tensor as input.
    const int batch_size = 1;
    const int height = 128;
    const int width = 128;
    const int channels = 3;
    std::vector<int> input_dims = {batch_size, height, width, channels};
    std::vector<float> input_data(batch_size * height * width * channels);

    // In a real application, you would load an image and preprocess it here.
    // Preprocessing would involve resizing to 128x128 and normalizing pixel values.
    // This example uses dummy data for demonstration.
    for (size_t i = 0; i < input_data.size(); ++i) {
        input_data[i] = static_cast<float>(i % 256) / 255.0f; // Example normalization
    }

    LiteRtTensor* input_tensor = LiteRtTensorCreate(
        kLiteRtFloat32, input_dims.data(), input_dims.size(),
        input_data.data(), input_data.size() * sizeof(float));

    std::vector<LiteRtTensor*> inputs = {input_tensor};

    // 4. Run Inference
    std::cout << "Running inference..." << std::endl;
    if (LiteRtInterpreterInvoke(interpreter, inputs.data(), inputs.size()) != kLiteRtOk) {
        std::cerr << "Failed to invoke interpreter." << std::endl;
        return 1;
    }
    std::cout << "Inference completed." << std::endl;


    // 5. Get Output Tensors
    int num_outputs = LiteRtInterpreterGetOutputTensorCount(interpreter);
    std::cout << "Number of output tensors: " << num_outputs << std::endl;

    for (int i = 0; i < num_outputs; ++i) {
        const LiteRtTensor* output_tensor = LiteRtInterpreterGetOutputTensor(interpreter, i);
        PrintTensorDetails(output_tensor, "Output " + std::to_string(i));

        // The BlazeFace model has two outputs:
        // - A [1, 896, 16] tensor for bounding boxes and keypoints.
        // - A [1, 896, 1] tensor for face scores.
        // You would add post-processing logic here to decode these tensors.
    }


    // 6. Clean up LiteRT resources
    LiteRtTensorDelete(input_tensor);
    LiteRtInterpreterDelete(interpreter);
    LiteRtInterpreterOptionsDelete(interpreter_options);
    LiteRtModelDelete(model);
    LiteRtRuntimeDelete(runtime);
    LiteRtRuntimeOptionsDelete(runtime_options);
    LiteRtEnvDelete(env);
    LiteRtEnvOptionsDelete(env_options);

    return 0;
}