#include <forge/xr.hpp>
#include <forge/gl.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#ifdef __ANDROID__
#include <forge/android_host.hpp>
#include <EGL/egl.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#else
#include <windows.h>
#include <unknwn.h>
#include <GL/gl.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
namespace forge {
namespace {
void check(XrResult result, const char *operation) {
    if (XR_FAILED(result)) throw std::runtime_error(std::string(operation) + " failed (OpenXR " + std::to_string(result) + ")");
}
template<class T> T function(XrInstance instance, const char *name) {
    PFN_xrVoidFunction fn = nullptr;
    check(xrGetInstanceProcAddr(instance, name, &fn), name);
    return reinterpret_cast<T>(fn);
}
glm::dmat4 poseMatrix(const XrPosef &pose, double scale) {
    auto q = pose.orientation;
    return glm::translate(glm::dmat4(1), glm::dvec3(pose.position.x, pose.position.y, pose.position.z) / scale) *
        glm::mat4_cast(glm::dquat(q.w, q.x, q.y, q.z));
}
constexpr XrSpaceLocationFlags validPose = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
constexpr XrViewStateFlags validViews = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
}
struct XrSession::Impl {
    XrSettings settings;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    ::XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE, head = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    XrTime displayTime = 0;
    XrDuration period = 0;
    bool running = false, begun = false, shouldRender = false, exit = false;
    bool viewsValid = false;
    glm::dmat4 rig{1}, pendingRig{1};
    std::string runtimeName;
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    struct Swapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        unsigned fbo = 0, depth = 0;
        int width = 0, height = 0;
        bool acquired = false, waited = false;
#ifdef __ANDROID__
        std::vector<XrSwapchainImageOpenGLESKHR> images;
#else
        std::vector<XrSwapchainImageOpenGLKHR> images;
#endif
    };
    std::array<Swapchain, 2> chains;
    XrActionSet actions = XR_NULL_HANDLE;
    XrAction grip = XR_NULL_HANDLE, aim = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE,
             stick = XR_NULL_HANDLE, primary = XR_NULL_HANDLE, secondary = XR_NULL_HANDLE,
             select = XR_NULL_HANDLE, menu = XR_NULL_HANDLE, vibration = XR_NULL_HANDLE;
    std::array<XrPath, 2> hands{};
    std::array<XrSpace, 2> grips{}, aims{};
    Json snapshot = {{"enabled", true}, {"running", false}, {"focused", false}, {"head", {{"valid", false}}}, {"hands", Json::object()}};
    XrPath path(const std::string &name) {
        XrPath result; check(xrStringToPath(instance, name.c_str(), &result), "xrStringToPath"); return result;
    }
    XrAction action(const char *name, XrActionType type) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(info.localizedActionName, name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        info.actionType = type; info.countSubactionPaths = 2; info.subactionPaths = hands.data();
        XrAction result; check(xrCreateAction(actions, &info, &result), "xrCreateAction"); return result;
    }
    void bindings(const char *profile, int type) {
        std::vector<XrActionSuggestedBinding> values;
        for (int hand = 0; hand < 2; ++hand) {
            std::string root = hand == 0 ? "/user/hand/left" : "/user/hand/right";
            auto add = [&](XrAction action, const char *component) { values.push_back({action, path(root + component)}); };
            add(grip, "/input/grip/pose"); add(aim, "/input/aim/pose"); add(vibration, "/output/haptic");
            if (type == 0) { add(select, "/input/select/click"); add(menu, "/input/menu/click"); }
            else {
                add(trigger, "/input/trigger/value");
                if (type == 3) { add(select, "/input/trigger/click"); add(stick, "/input/trackpad"); add(menu, "/input/menu/click"); }
                else {
                    add(squeeze, type == 2 ? "/input/squeeze/force" : "/input/squeeze/value");
                    add(stick, "/input/thumbstick");
                    add(primary, type == 1 && hand == 0 ? "/input/x/click" : "/input/a/click");
                    add(secondary, type == 1 && hand == 0 ? "/input/y/click" : "/input/b/click");
                    if (type == 1 && hand == 0) add(menu, "/input/menu/click");
                }
            }
        }
        XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        info.interactionProfile = path(profile); info.countSuggestedBindings = uint32_t(values.size()); info.suggestedBindings = values.data();
        check(xrSuggestInteractionProfileBindings(instance, &info), "xrSuggestInteractionProfileBindings");
    }
    void createActions() {
        hands = {path("/user/hand/left"), path("/user/hand/right")};
        XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(info.actionSetName, "gameplay"); std::strcpy(info.localizedActionSetName, "Gameplay");
        check(xrCreateActionSet(instance, &info, &actions), "xrCreateActionSet");
        grip = action("grip", XR_ACTION_TYPE_POSE_INPUT); aim = action("aim", XR_ACTION_TYPE_POSE_INPUT);
        trigger = action("trigger", XR_ACTION_TYPE_FLOAT_INPUT); squeeze = action("squeeze", XR_ACTION_TYPE_FLOAT_INPUT);
        stick = action("stick", XR_ACTION_TYPE_VECTOR2F_INPUT); select = action("select", XR_ACTION_TYPE_BOOLEAN_INPUT);
        primary = action("primary", XR_ACTION_TYPE_BOOLEAN_INPUT); secondary = action("secondary", XR_ACTION_TYPE_BOOLEAN_INPUT);
        menu = action("menu", XR_ACTION_TYPE_BOOLEAN_INPUT); vibration = action("vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT);
        bindings("/interaction_profiles/khr/simple_controller", 0);
        bindings("/interaction_profiles/oculus/touch_controller", 1);
        bindings("/interaction_profiles/valve/index_controller", 2);
        bindings("/interaction_profiles/htc/vive_controller", 3);
        for (int hand = 0; hand < 2; ++hand) {
            XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            spaceInfo.subactionPath = hands[hand]; spaceInfo.poseInActionSpace.orientation.w = 1;
            spaceInfo.action = grip; check(xrCreateActionSpace(session, &spaceInfo, &grips[hand]), "xrCreateActionSpace(grip)");
            spaceInfo.action = aim; check(xrCreateActionSpace(session, &spaceInfo, &aims[hand]), "xrCreateActionSpace(aim)");
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1; attach.actionSets = &actions;
        check(xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets");
    }
    void initialize(const std::string &application) {
#ifdef __ANDROID__
        auto init = function<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE, "xrInitializeLoaderKHR");
        XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInfo.applicationVM = androidApp()->activity->vm; loaderInfo.applicationContext = androidApp()->activity->clazz;
        check(init(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR *>(&loaderInfo)), "xrInitializeLoaderKHR");
        const char *extensions[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
#else
        const char *extensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
#endif
        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        size_t nameSize = std::min(application.size(), size_t(XR_MAX_APPLICATION_NAME_SIZE - 1));
        if (nameSize < application.size())
            while (nameSize && (static_cast<unsigned char>(application[nameSize]) & 0xc0) == 0x80) --nameSize;
        std::memcpy(create.applicationInfo.applicationName, application.data(), nameSize);
        std::strcpy(create.applicationInfo.engineName, "Forge"); create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        create.enabledExtensionCount = uint32_t(std::size(extensions)); create.enabledExtensionNames = extensions;
#ifdef __ANDROID__
        XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        androidInfo.applicationVM = androidApp()->activity->vm; androidInfo.applicationActivity = androidApp()->activity->clazz;
        create.next = &androidInfo;
#endif
        check(xrCreateInstance(&create, &instance), "xrCreateInstance (install/select an OpenXR runtime)");
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES}; check(xrGetInstanceProperties(instance, &properties), "xrGetInstanceProperties");
        runtimeName = properties.runtimeName;
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        check(xrGetSystem(instance, &systemInfo, &system), "xrGetSystem (connect/wake the headset)");
#ifdef __ANDROID__
        XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        check(function<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(instance, "xrGetOpenGLESGraphicsRequirementsKHR")(instance, system, &requirements), "xrGetOpenGLESGraphicsRequirementsKHR");
        XrGraphicsBindingOpenGLESAndroidKHR graphics{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        graphics.display = eglGetCurrentDisplay(); graphics.context = eglGetCurrentContext(); graphics.config = androidEglConfig();
        XrVersion graphicsVersion = XR_MAKE_VERSION(3, 2, 0);
#else
        XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        check(function<PFN_xrGetOpenGLGraphicsRequirementsKHR>(instance, "xrGetOpenGLGraphicsRequirementsKHR")(instance, system, &requirements), "xrGetOpenGLGraphicsRequirementsKHR");
        XrGraphicsBindingOpenGLWin32KHR graphics{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
        graphics.hDC = wglGetCurrentDC(); graphics.hGLRC = wglGetCurrentContext();
        int major = 0, minor = 0; gl::GetIntegerv(0x821B, &major); gl::GetIntegerv(0x821C, &minor);
        XrVersion graphicsVersion = XR_MAKE_VERSION(major, minor, 0);
#endif
        if (graphicsVersion < requirements.minApiVersionSupported || graphicsVersion > requirements.maxApiVersionSupported)
            throw std::runtime_error("Current OpenGL context is outside the OpenXR runtime's supported version range");
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO}; sessionInfo.systemId = system; sessionInfo.next = &graphics;
        check(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession");
        XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; reference.poseInReferenceSpace.orientation.w = 1;
        reference.referenceSpaceType = settings.space == "stage" ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
        check(xrCreateReferenceSpace(session, &reference, &space), "xrCreateReferenceSpace (requested space unavailable)");
        reference.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        check(xrCreateReferenceSpace(session, &reference, &head), "xrCreateReferenceSpace(view)");
        uint32_t count = 0;
        check(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "xrEnumerateViewConfigurationViews");
        if (count != 2) throw std::runtime_error("Forge XR requires primary stereo with two views");
        std::array<XrViewConfigurationView, 2> configurations{{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        check(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, configurations.data()), "xrEnumerateViewConfigurationViews");
        check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
        std::vector<int64_t> formats(count); check(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "xrEnumerateSwapchainFormats");
        // Linear RGBA8: Forge currently produces display-referred output without a linear-light pipeline.
        constexpr int64_t rgba8 = 0x8058;
        if (std::find(formats.begin(), formats.end(), rgba8) == formats.end()) throw std::runtime_error("XR runtime does not offer GL_RGBA8 swapchains");
        int maxSize = 0; gl::GetIntegerv(gl::MAX_TEXTURE_SIZE, &maxSize);
        uint64_t eyeBytes = 0;
        for (int eye = 0; eye < 2; ++eye) {
            auto &chain = chains[eye]; const auto &c = configurations[eye];
            if (!c.recommendedImageRectWidth || !c.recommendedImageRectHeight || c.recommendedImageRectWidth > uint32_t(maxSize) || c.recommendedImageRectHeight > uint32_t(maxSize))
                throw std::runtime_error("XR recommended eye size exceeds GPU limits");
            chain.width = int(c.recommendedImageRectWidth); chain.height = int(c.recommendedImageRectHeight);
            XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; sc.format = rgba8; sc.sampleCount = 1;
            sc.width = chain.width; sc.height = chain.height; sc.faceCount = sc.arraySize = sc.mipCount = 1;
            check(xrCreateSwapchain(session, &sc, &chain.handle), "xrCreateSwapchain");
            check(xrEnumerateSwapchainImages(chain.handle, 0, &count, nullptr), "xrEnumerateSwapchainImages");
            if (!count || count > 16) throw std::runtime_error("XR swapchain image count is outside Forge's bound of 1..16");
            uint64_t bytes = uint64_t(chain.width) * chain.height * 4 * (count + 1);
            if (bytes > settings.eyeBudget || eyeBytes > settings.eyeBudget - bytes)
                throw std::runtime_error("XR eye buffers exceed xr.eye_budget_bytes");
            eyeBytes += bytes;
#ifdef __ANDROID__
            chain.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
            chain.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
            check(xrEnumerateSwapchainImages(chain.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader *>(chain.images.data())), "xrEnumerateSwapchainImages");
            gl::GenFramebuffers(1, &chain.fbo); gl::GenTextures(1, &chain.depth);
            gl::BindTexture(gl::TEXTURE_2D, chain.depth);
            gl::TexImage2D(gl::TEXTURE_2D, 0, gl::DEPTH_COMPONENT24, chain.width, chain.height, 0, gl::DEPTH_COMPONENT, gl::UNSIGNED_INT, nullptr);
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR); gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        }
        createActions();
    }
    Json locate(XrSpace target, bool active = true) {
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (!active) return {{"valid", false}, {"tracked", false}};
        check(xrLocateSpace(target, space, displayTime, &location), "xrLocateSpace");
        if ((location.locationFlags & validPose) != validPose) return {{"valid", false}, {"tracked", false}};
        auto matrix = rig * poseMatrix(location.pose, settings.metersPerUnit);
        auto p = glm::dvec3(matrix[3]); auto q = glm::quat_cast(glm::dmat3(matrix));
        return {{"valid", true}, {"tracked", bool((location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT))},
                {"position", {p.x, p.y, p.z}}, {"orientation", {q.x, q.y, q.z, q.w}}};
    }
    void sample() {
        bool focused = sessionState == XR_SESSION_STATE_FOCUSED;
        snapshot = {{"enabled", true}, {"running", running}, {"focused", focused}, {"runtime", runtimeName},
                    {"session_state", int(sessionState)}, {"should_render", shouldRender}, {"views_valid", viewsValid},
                    {"predicted_time_ns", displayTime}, {"predicted_period_ns", period},
                    {"reference_space", settings.space}, {"head", running ? locate(head) : Json{{"valid", false}}}, {"hands", Json::object()}};
        if (!focused) return; // never expose stale button/axis state outside focus
        XrActiveActionSet set{actions, XR_NULL_PATH}; XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1; sync.activeActionSets = &set; check(xrSyncActions(session, &sync), "xrSyncActions");
        for (int hand = 0; hand < 2; ++hand) {
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO}; get.subactionPath = hands[hand];
            auto poseActive = [&](XrAction action) { get.action = action; XrActionStatePose value{XR_TYPE_ACTION_STATE_POSE}; check(xrGetActionStatePose(session, &get, &value), "xrGetActionStatePose"); return bool(value.isActive); };
            Json values = {{"grip", locate(grips[hand], poseActive(grip))}, {"aim", locate(aims[hand], poseActive(aim))}};
            auto scalar = [&](XrAction action) { get.action = action; XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT}; check(xrGetActionStateFloat(session, &get, &value), "xrGetActionStateFloat"); return value.isActive ? value.currentState : 0.f; };
            values["trigger"] = scalar(trigger); values["squeeze"] = scalar(squeeze);
            auto boolean = [&](XrAction action) { get.action = action; XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN}; check(xrGetActionStateBoolean(session, &get, &value), "xrGetActionStateBoolean"); return value.isActive && value.currentState; };
            values["select"] = boolean(select); values["primary"] = boolean(primary); values["secondary"] = boolean(secondary); values["menu"] = boolean(menu);
            get.action = stick; XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F}; check(xrGetActionStateVector2f(session, &get, &value), "xrGetActionStateVector2f");
            values["stick"] = value.isActive ? Json::array({value.currentState.x, value.currentState.y}) : Json::array({0, 0});
            snapshot["hands"][hand == 0 ? "left" : "right"] = std::move(values);
        }
    }
    void release(Swapchain &chain) {
        if (!chain.acquired) return;
        if (!chain.waited) { XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout = XR_INFINITE_DURATION; check(xrWaitSwapchainImage(chain.handle, &wait), "xrWaitSwapchainImage"); chain.waited = true; }
        XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; check(xrReleaseSwapchainImage(chain.handle, &info), "xrReleaseSwapchainImage");
        chain.acquired = chain.waited = false;
    }
    void end(bool layer) {
        if (!begun) return;
        for (auto &chain : chains) release(chain);
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.space = space; projection.viewCount = 2; projection.views = projectionViews.data();
        const XrCompositionLayerBaseHeader *layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime = displayTime; end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = layer ? 1 : 0; end.layers = layer ? layers : nullptr;
        begun = false; check(xrEndFrame(session, &end), "xrEndFrame");
    }
    ~Impl() {
        try { end(false); } catch (...) {}
        for (auto &chain : chains) {
            if (chain.fbo) gl::DeleteFramebuffers(1, &chain.fbo);
            if (chain.depth) gl::DeleteTextures(1, &chain.depth);
            if (chain.handle) xrDestroySwapchain(chain.handle);
        }
        for (auto value : grips) if (value) xrDestroySpace(value);
        for (auto value : aims) if (value) xrDestroySpace(value);
        if (head) xrDestroySpace(head);
        if (space) xrDestroySpace(space);
        if (session) xrDestroySession(session);
        if (actions) xrDestroyActionSet(actions);
        if (instance) xrDestroyInstance(instance);
    }
};
XrSession::XrSession(const XrSettings &settings, const std::string &application) : impl(std::make_unique<Impl>()) {
    impl->settings = settings; impl->initialize(application);
}
XrSession::~XrSession() = default;
void XrSession::cancelFrame() noexcept { try { impl->end(false); } catch (...) {} }
bool XrSession::closing() const { return impl->exit; }
Json XrSession::state() const { return impl->snapshot; }
void XrSession::poll() {
    auto &s = *impl; s.end(false); s.rig = s.pendingRig;
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult eventResult;
    while ((eventResult = xrPollEvent(s.instance, &event)) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) s.exit = true;
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto change = reinterpret_cast<XrEventDataSessionStateChanged *>(&event); s.sessionState = change->state;
            if (change->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                check(xrBeginSession(s.session, &begin), "xrBeginSession"); s.running = true;
            }
            if (change->state == XR_SESSION_STATE_STOPPING) { s.running = false; check(xrEndSession(s.session), "xrEndSession"); }
            if (change->state == XR_SESSION_STATE_EXITING || change->state == XR_SESSION_STATE_LOSS_PENDING) { s.exit = true; s.running = false; }
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    check(eventResult, "xrPollEvent");
    s.shouldRender = s.viewsValid = false;
    if (s.running && !s.exit) {
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState frame{XR_TYPE_FRAME_STATE};
        check(xrWaitFrame(s.session, &wait, &frame), "xrWaitFrame"); s.displayTime = frame.predictedDisplayTime; s.period = frame.predictedDisplayPeriod;
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO}; check(xrBeginFrame(s.session, &begin), "xrBeginFrame"); s.begun = true; s.shouldRender = frame.shouldRender;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO}; locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = s.displayTime; locate.space = s.space;
        XrViewState state{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
        check(xrLocateViews(s.session, &locate, &state, 2, &count, s.views.data()), "xrLocateViews");
        s.viewsValid = count == 2 && (state.viewStateFlags & validViews) == validViews;
    }
    s.sample();
}
void XrSession::render(const std::function<void(const XrEye &)> &draw) {
    auto &s = *impl;
    try {
        if (s.begun && s.shouldRender && s.viewsValid) {
            for (int eye = 0; eye < 2; ++eye) {
                auto &chain = s.chains[eye]; const auto &view = s.views[eye]; uint32_t index = 0;
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; check(xrAcquireSwapchainImage(chain.handle, &acquire, &index), "xrAcquireSwapchainImage"); chain.acquired = true;
                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout = XR_INFINITE_DURATION;
                check(xrWaitSwapchainImage(chain.handle, &wait), "xrWaitSwapchainImage"); chain.waited = true;
                gl::BindFramebuffer(gl::FRAMEBUFFER, chain.fbo);
                gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, chain.images.at(index).image, 0);
                gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::TEXTURE_2D, chain.depth, 0);
                if (gl::CheckFramebufferStatus(gl::FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE) throw std::runtime_error("Incomplete XR eye framebuffer");
                auto worldFromEye = s.rig * poseMatrix(view.pose, s.settings.metersPerUnit);
                auto fov = view.fov;
                XrEye target;
                target.clipFromWorld = glm::mat4(xrProjection(fov.angleLeft, fov.angleRight, fov.angleDown, fov.angleUp, s.settings.nearZ, s.settings.farZ) * glm::inverse(worldFromEye));
                target.position = glm::vec3(worldFromEye[3]); target.width = chain.width; target.height = chain.height; target.framebuffer = chain.fbo;
                draw(target); // must never execute game updates once per eye
                auto flush = reinterpret_cast<void (FORGE_GL_CALL *)()>(glfwGetProcAddress("glFlush"));
                if (!flush) throw std::runtime_error("Missing glFlush for XR submission");
                flush();
                auto &projection = s.projectionViews[eye]; projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                projection.pose = view.pose; projection.fov = view.fov; projection.subImage.swapchain = chain.handle;
                projection.subImage.imageRect.extent = {chain.width, chain.height}; s.release(chain);
            }
            s.end(true);
        } else s.end(false);
    } catch (...) { cancelFrame(); gl::BindFramebuffer(gl::FRAMEBUFFER, 0); throw; }
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
}
void XrSession::origin(glm::dvec3 position, double yaw) {
    for (double v : {position.x, position.y, position.z, yaw}) if (!std::isfinite(v) || std::abs(v) > 1e9) throw std::runtime_error("XR origin must be finite and bounded to +/-1e9");
    impl->pendingRig = glm::translate(glm::dmat4(1), position) * glm::rotate(glm::dmat4(1), glm::radians(std::remainder(yaw, 360.0)), glm::dvec3(0, 1, 0));
}
bool XrSession::haptic(const std::string &hand, double amplitude, double duration, double frequency) {
    if (hand != "left" && hand != "right") throw std::runtime_error("XR hand must be left or right");
    if (!std::isfinite(amplitude) || amplitude < 0 || amplitude > 1 || !std::isfinite(duration) || duration <= 0 || duration > 5 || !std::isfinite(frequency) || frequency < 0 || frequency > 1000)
        throw std::runtime_error("XR haptic requires amplitude 0..1, duration (0..5] seconds, frequency 0..1000 Hz");
    auto &s = *impl; if (!s.running || s.sessionState != XR_SESSION_STATE_FOCUSED) return false;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO}; info.action = s.vibration; info.subactionPath = s.hands[hand == "left" ? 0 : 1];
    XrHapticVibration value{XR_TYPE_HAPTIC_VIBRATION}; value.amplitude = float(amplitude); value.duration = XrDuration(duration * 1e9); value.frequency = float(frequency);
    check(xrApplyHapticFeedback(s.session, &info, reinterpret_cast<const XrHapticBaseHeader *>(&value)), "xrApplyHapticFeedback"); return true;
}
} // namespace forge
