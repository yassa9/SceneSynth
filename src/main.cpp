#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// #include <torch/script.h> // One-stop header.
// #include <torch/torch.h>
// #include <opencv2/opencv.hpp>

#include <Python.h>

#include "../utils/ImGuiFileDialog/ImGuiFileDialog.h"

#define EXIT_SUCCESS 0

#include <filesystem>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to
// maximize ease of testing and compatibility with old VS compilers. To link
// with VS2010-era libraries, VS2015+ requires linking with
// legacy_stdio_definitions.lib, which we do using this pragma. Your own project
// should not be affected, as you are likely to link with a newer binary of GLFW
// that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) &&                                 \
    !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See
// 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

static void glfw_error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int GLFWwindowHeight {720};
int GLFWwindowWidth {1280};
static bool checkImgWindowResizable = false;

// Global variable to keep track of the dialog state, and images
bool showFileDialog = false;
std::string filePath = "";
std::vector<std::string> imagePaths;
std::string selectedImagePath;
GLuint my_image_texture = 0; // Global variable to hold the texture
std::string outputExecution = "";

// ############################################################################################
// Some Useful Functions
// ############################################################################################

// Function to scan directory for images
void scanDirectoryForImages(const std::string& directoryPath)
{
    imagePaths.clear();
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath))
    {
        if (entry.is_regular_file())
        {
            std::string extension = entry.path().extension().string();
            if (extension == ".jpg" || extension == ".png" || extension == ".bmp" || extension == ".jpeg")
            {
                imagePaths.push_back(entry.path().string());
            }
        }
    }
}

void openFileDialogToolBar()
{
    if (ImGui::Button("Open"))
    {
        showFileDialog = true;
        IGFD::FileDialogConfig config;
	    config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", nullptr, config);
    }

    if (showFileDialog)
    {
        if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
        {
            // action if OK
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                scanDirectoryForImages(filePath);
            }
            // close
            ImGuiFileDialog::Instance()->Close();
            showFileDialog = false;
        }
    }
}

// Simple helper function to load an image into an OpenGL texture with common settings
bool LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height)
{
    if (*out_texture != 0) {
        glDeleteTextures(1, out_texture);
        *out_texture = 0;
    }

    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

// Function to update selectedImagePath to point to the output image
void updateSelectedImagePath(std::string& imagePath)
{
    std::filesystem::path path(imagePath);
    path = path.parent_path() / "outputs" / path.filename();
    imagePath = path.string();
}

void jobsExecution(std::string& jobModel)
{
    // Set the Python script path
    std::string scriptPath = "../py_assets/detect_y8.py";

    // Redirect stdout to a file
    std::string outputFile = "../py_assets/outputExecution.txt";
    freopen(outputFile.c_str(), "w", stdout);

    // Construct the Python command to run the script with an argument
    std::ostringstream command;
    command << "import sys; sys.path.append('../py_assets'); ";
    command << "from detect_y8 import detect_and_save_image; ";
    command << "detect_and_save_image('" << selectedImagePath << "', '" << jobModel << "')";

    // Run the constructed command
    PyRun_SimpleString(command.str().c_str());

    // Close the file and restore stdout
    fclose(stdout);
    freopen("/dev/tty", "w", stdout);

    // Read the output from the file into a variable
    std::ifstream outputFileStream(outputFile);
    outputExecution.assign((std::istreambuf_iterator<char>(outputFileStream)), std::istreambuf_iterator<char>());
    std::cout << "Python outputExecution: " << outputExecution << std::endl;

    // Invalidate the old texture
    if (my_image_texture != 0) {
        glDeleteTextures(1, &my_image_texture);
        my_image_texture = 0;
    }

    updateSelectedImagePath(selectedImagePath);
}

// ############################################################################################
// Main Function
// ############################################################################################

// Main code
int main(int, char **)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char *glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+
    // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow *window = glfwCreateWindow(GLFWwindowWidth, GLFWwindowHeight, "FlorenceFlame", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Customize colors
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(32.0f, 32.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 4.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 2.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.FrameRounding = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 13.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabMinSize = 7.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.TabBorderSize = 1.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.28f, 0.28f, 0.28f, 0.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.27f, 0.27f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 0.39f, 0.0f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.39f, 0.39f, 0.39f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.39f, 0.0f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.19f, 0.19f, 0.4f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.391f);
    colors[ImGuiCol_Header] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.47f, 0.47f, 0.47f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.47f, 0.47f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.39f, 0.39f, 0.39f, 1.0f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 0.39f, 0.0f, 1.0f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 0.39f, 0.0f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.47f, 0.47f, 0.47f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.39f, 0.0f, 1.0f);

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can
    // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
    // them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
    // need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please
    // handle those errors in your application (e.g. use an assertion, or display
    // an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored
    // into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which
    // ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype
    // for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string
    // literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at
    // runtime from the "fonts/" folder. See Makefile.emscripten for details.
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    // ImFont* font =
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f,
    // nullptr, io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != nullptr);


    // Our state
    // bool show_another_window = true;
    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 1.00f);
    bool shouldClose = false;

    int my_image_width = 0, my_image_height = 0; // Variables to hold image dimensions

    static int radioModelJob = 0;
    static bool textCopied = false;

    Py_Initialize();
    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        if (shouldClose)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
        // tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
        // your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
        // data to your main application, or clear/overwrite your copy of the
        // keyboard data. Generally you may always pass all inputs to dear imgui,
        // and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in
        // ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear
        // ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

// ################################################################################
// ################################################################################

    {
        int toolBarHeight = 40;
        int leftLayoutWidth = 200;
        int rightLayoutWidth = 400;
        int middleLayoutWidth = ImGui::GetIO().DisplaySize.x - leftLayoutWidth - rightLayoutWidth;
        int allLayoutHeight = ImGui::GetIO().DisplaySize.y - toolBarHeight;

        // Custom Toolbar
        ImGuiWindowFlags custom_toolbar_window_flags = ImGuiWindowFlags_NoTitleBar|
                                                       ImGuiWindowFlags_NoResize  |
                                                         ImGuiWindowFlags_NoMove  |
                                                    ImGuiWindowFlags_NoScrollbar  |
                                               ImGuiWindowFlags_NoScrollWithMouse ;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, toolBarHeight));
        ImGui::Begin("Toolbar", NULL, custom_toolbar_window_flags);

        openFileDialogToolBar();

        ImGui::SameLine();
        if (ImGui::Button("Run"))
        {
        }

        ImGui::SameLine();
        ImGui::Checkbox("DemWin", &show_demo_window);

        ImGui::SameLine();
        ImGui::Checkbox("ImgWindowResizable", &checkImgWindowResizable);

        // Calculate the position for the close button
        float buttonWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(io.DisplaySize.x - buttonWidth - ImGui::GetStyle().WindowPadding.x);

        if (ImGui::Button("Close")) {
            shouldClose = true;
        }

        ImGui::End();

        ImGuiWindowFlags window_flags = 0;

        ImGui::SetNextWindowPos(ImVec2(0, toolBarHeight));
        ImGui::SetNextWindowSize(ImVec2(leftLayoutWidth, allLayoutHeight));
        ImGui::Begin("Gadgets", NULL, window_flags);

            ImGui::RadioButton("Detect", &radioModelJob, 0);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Perform Object Detection.");
            }
            ImGui::RadioButton("Classify", &radioModelJob, 1);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Perform Object Classification.");
            }
            ImGui::RadioButton("Segment", &radioModelJob, 2);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Perform Object Segmentation.");
            }
            ImGui::RadioButton("Poser", &radioModelJob, 3);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Perform Pose Estimation.");
            }
            ImGui::RadioButton("Depth", &radioModelJob, 4);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Perform Depth Estimation.");
            }

            ImGui::Spacing();
            if (ImGui::Button("Run"))
            {
                textCopied = false;
                switch (radioModelJob)
                {
                    case 0:
                    {
                        std::string jobModel = "yolov8l.pt";
                        jobsExecution(jobModel);
                        break;
                    }

                    case 1:
                    {
                        std::string jobModel = "yolov8l-cls.pt";
                        jobsExecution(jobModel);
                        break;
                    }

                    case 2:
                    {
                        std::string jobModel = "yolov8l-seg.pt";
                        jobsExecution(jobModel);
                        break;
                    }

                    case 3:
                    {
                        std::string jobModel = "yolov8l-pose.pt";
                        jobsExecution(jobModel);
                        break;
                    }

                    case 4:
                    {
                        // Set the Python script path
                        std::string scriptPath = "../py_assets/glpn.py";

                        // Construct the Python command to run the script with an argument
                        std::ostringstream command;
                        command << "import sys; sys.path.append('../py_assets'); ";
                        command << "from detr import estimate_and_save_depth; ";
                        command << "estimate_and_save_depth('" << selectedImagePath << "')";

                        // Run the constructed command
                        PyRun_SimpleString(command.str().c_str());

                        // Invalidate the old texture
                        if (my_image_texture != 0) {
                            glDeleteTextures(1, &my_image_texture);
                            my_image_texture = 0;
                        }

                        updateSelectedImagePath(selectedImagePath);
                        break;
                    }

                    default:
                    {
                        std::string jobModel = "yolov8l.pt";
                        jobsExecution(jobModel);
                        break;
                    }
                }

            }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - rightLayoutWidth, toolBarHeight));
        ImGui::SetNextWindowSize(ImVec2(rightLayoutWidth, allLayoutHeight));
        ImGui::Begin("Monitoring", NULL, window_flags);

        // Child window with a rounded border for displaying image list
        {
            ImGuiWindowFlags child_window_flags = ImGuiWindowFlags_None;
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild("ChildR", ImVec2(0, 0.33*allLayoutHeight), ImGuiChildFlags_Border, child_window_flags);

            if (!imagePaths.empty())
            {
                for (const auto& imagePath : imagePaths)
                {
                    if (ImGui::Selectable(imagePath.c_str(), selectedImagePath == imagePath)) {
                        selectedImagePath = imagePath; // Store the selected image path
                        if (my_image_texture != 0) {
                            glDeleteTextures(1, &my_image_texture);
                            my_image_texture = 0;
                        }
                    }
                }
            }
            else
            {
                ImGui::TextWrapped("No images found in the directory");
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        ImGui::TextWrapped("Directory Path Selected:");
        ImGui::TextWrapped("%s", filePath.c_str());
        ImGui::TextWrapped("%s", selectedImagePath.c_str());
        ImGui::NewLine();

        ImGui::TextWrapped("Application average: ");
        ImGui::TextWrapped("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        ImGui::NewLine();
        ImGui::TextWrapped("Image dimensions:");
        ImGui::TextWrapped("%d x %d", my_image_width, my_image_height);

        ImGui::NewLine();
        ImGui::End();

        // CENTRAL IMAGE DISPLAY
        int centralImgWindowHeight = static_cast<int>(allLayoutHeight * 4 / 5);
        if (!checkImgWindowResizable)
        {
            ImGui::SetNextWindowSize(ImVec2(middleLayoutWidth, centralImgWindowHeight));
            ImGui::SetNextWindowPos(ImVec2(leftLayoutWidth, toolBarHeight));
        }

        ImGui::Begin("Image Window", NULL, ImGuiWindowFlags_None);

        // Load and display selected image
        if (!selectedImagePath.empty() && my_image_texture == 0)
        {
            bool ret = LoadTextureFromFile(selectedImagePath.c_str(), &my_image_texture, &my_image_width, &my_image_height);
            IM_ASSERT(ret);
        }

        if (my_image_texture != 0)
        {
            ImVec2 newImgSize = ImVec2(my_image_width, my_image_height);
            ImVec2 windowSize = ImGui::GetWindowSize();
            float windowAspectRatio = windowSize.x / windowSize.y;
            float imageAspectRatio = static_cast<float>(my_image_width) / my_image_height;

            if (!(my_image_width < windowSize.x && my_image_height < windowSize.y))
            {
                if (imageAspectRatio > windowAspectRatio)
                {
                    newImgSize.x = windowSize.x;
                    newImgSize.y = newImgSize.x / imageAspectRatio;
                }
                else
                {
                    newImgSize.y = 0.75 * windowSize.y;
                    newImgSize.x = newImgSize.y * imageAspectRatio;
                }
            }
            ImGui::Image((void*)(intptr_t)my_image_texture, newImgSize);

            float newToOldRatio = my_image_height / newImgSize.y;

            ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImGui::IsItemHovered())
            {
                ImGuiIO& io = ImGui::GetIO();
                float region_x = io.MousePos.x - pos.x;
                float region_y = io.MousePos.y - pos.y + newImgSize.y;
                ImGui::BeginTooltip();
                ImGui::Text("X-ratiod: %.2f", region_x);
                ImGui::Text("Y-ratiod: %.2f", region_y);
                ImGui::NewLine();
                ImGui::Text("X-coord: %.2f", region_x * newToOldRatio);
                ImGui::Text("Y-coord: %.2f", region_y * newToOldRatio);
                ImGui::NewLine();
                ImGui::Text("mousePos: %.2f", io.MousePos.x);
                ImGui::Text("mousePos: %.2f", io.MousePos.y);

                ImGui::EndTooltip();
            }
        }

        else
            ImGui::TextWrapped("No image selected or failed to load.");

        ImGui::End();

        // CENTRAL LOWER WINDOW
        ImGui::SetNextWindowPos(ImVec2(leftLayoutWidth, toolBarHeight + centralImgWindowHeight));
        ImGui::SetNextWindowSize(ImVec2(middleLayoutWidth, allLayoutHeight - centralImgWindowHeight));
        ImGui::Begin("Execution Info", NULL, window_flags);
        // Child window with a rounded border for displaying image list
        {
            ImGuiWindowFlags child_window_flags = ImGuiWindowFlags_None;
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

            // Create a copy button
            if (ImGui::Button("Copy")) {
                ImGui::SetClipboardText(outputExecution.c_str());
                textCopied = true;  // Set the flag to true when the text is copied
            }

            // Display "copied to clipboard!" message if the text was copied
            if (textCopied) {
                ImGui::SameLine();  // Display the message on the same line as the button
                ImGui::Text("Copied to clipboard!");
            }

            ImGui::BeginChild("ChildR", ImVec2(0, 0.55*(allLayoutHeight - centralImgWindowHeight)), ImGuiChildFlags_Border, child_window_flags);
            {
                ImGui::TextWrapped("%s", outputExecution.c_str());
                ImGui::Spacing();
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        ImGui::End();
    }

// ################################################################################
// ################################################################################

    /*
    // 2. Show a simple window that we create ourselves. We use a Begin/End pair
    // to create a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!"
                                     // and append into it.

        ImGui::Text("This is some useful text."); // Display some text (you can
                                                // use a format strings too)

        ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state

        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float *)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button")) // Buttons return true when clicked (most
                                   // widgets return true when edited/activated)
            counter++;

        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / io.Framerate, io.Framerate);


        // Child 2: rounded border
        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild("ChildR", ImVec2(0, 460), ImGuiChildFlags_Border, window_flags);
            if (ImGui::BeginTable("split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
            {
                for (int i = 0; i < 10; i++)
                {
                    char buf[32];
                    sprintf(buf, "%03d", i);
                    ImGui::TableNextColumn();
                    ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window); // Pass a pointer to our bool variable (the
                                 // window will have a closing button that will
                                 // clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }
    */

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

  }

    Py_Finalize();
#ifdef __EMSCRIPTEN__
  EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

  return EXIT_SUCCESS;

}
