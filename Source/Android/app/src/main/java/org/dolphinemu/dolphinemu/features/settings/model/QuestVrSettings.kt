// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.settings.model

import org.dolphinemu.dolphinemu.BuildConfig
import org.dolphinemu.dolphinemu.features.input.model.controlleremu.EmulatedController

object QuestVrSettings {
    const val STEREO_MODE_OPENXR = 6

    private const val GC_PROFILE_NAME = "Quest Touch GameCube.ini"
    private const val WIIMOTE_PROFILE_NAME = "OpenXR Wii Remote.ini"
    private const val HOTKEY_PROFILE_NAME = "Quest.ini"
    private const val OPENXR_DEVICE = "OpenXR/0/OpenXR Controller"
    private const val VR_SECTION = "VR"

    private fun androidBooleanSetting(key: String, defaultValue: Boolean) =
        AdHocBooleanSetting(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, key, defaultValue)

    private fun androidIntSetting(key: String, defaultValue: Int) =
        AdHocIntSetting(Settings.FILE_DOLPHIN, Settings.SECTION_INI_ANDROID, key, defaultValue)

    private fun vrBooleanSetting(key: String, defaultValue: Boolean) =
        AdHocBooleanSetting(Settings.FILE_GFX, VR_SECTION, key, defaultValue)

    private fun vrIntSetting(key: String, defaultValue: Int) =
        AdHocIntSetting(Settings.FILE_GFX, VR_SECTION, key, defaultValue)

    private fun vrFloatSetting(key: String, defaultValue: Float) =
        AdHocFloatSetting(Settings.FILE_GFX, VR_SECTION, key, defaultValue)

    fun isQuestBuild(): Boolean = BuildConfig.IS_QUEST

    fun openXrEnabledSetting() = androidBooleanSetting("QuestOpenXREnabled", true)

    fun launchInVrSetting() = androidBooleanSetting("QuestLaunchInVr", true)

    fun flatScreenSetting() = vrBooleanSetting("FlatScreen", false)

    fun recenterOnLaunchSetting() = androidBooleanSetting("QuestRecenterOnLaunch", true)

    fun leftHandedSetting() = androidBooleanSetting("QuestLeftHanded", false)

    fun showMirrorSurfaceSetting() = androidBooleanSetting("QuestShowMirrorSurface", false)

    fun autoVbiFromHmdSetting() = vrBooleanSetting("AutoVBIFromHMD", false)

    fun unitsPerMeterSetting(): AbstractFloatSetting = FloatSetting.GFX_VR_UNITS_PER_METER

    fun leanBackAngleSetting() = vrFloatSetting("LeanBackAngle", 0.0f)

    fun cameraForwardSetting() = vrFloatSetting("CameraForward", 0.0f)

    fun cameraHeightSetting() = vrFloatSetting("CameraHeight", 0.0f)

    fun detectSkyboxSetting() = vrBooleanSetting("DetectSkybox", false)

    fun passthroughSetting() = vrBooleanSetting("Passthrough", false)

    fun passthroughRevealUnrenderedSetting() =
        vrBooleanSetting("PassthroughRemoveBlackBackground", true)

    fun passthroughRemoveBlackClearsSetting() =
        vrBooleanSetting("PassthroughRemoveBlackEFBClears", true)

    fun passthroughSceneOpacitySetting() = vrFloatSetting("PassthroughSceneOpacity", 1.0f)

    fun vrGammaSetting() = vrFloatSetting("Gamma", 1.0f)

    fun exactScreenDepthSetting() = vrBooleanSetting("ExactScreenDepth", true)

    fun removeBarsSetting() = vrBooleanSetting("RemoveCinematicBars", true)

    fun useVulkanMultiviewSetting() = vrBooleanSetting("UseVulkanMultiview", true)

    fun androidDirectToHmdSetting() = vrBooleanSetting("AndroidDirectToHMD", true)

    fun cpuLevel5HintSetting() = vrBooleanSetting("QuestCpuLevel5Hint", false)

    // Defaults must match GraphicsSettings.cpp (Android values).
    fun resolutionScaleSetting() = vrFloatSetting("ResolutionScale", 0.85f)

    fun foveationLevelSetting() = vrIntSetting("FoveationLevel", 2)

    fun dynamicFoveationSetting() = vrBooleanSetting("DynamicFoveation", true)

    // Off by default: forces binned rendering, which hurts EFB-copy-heavy games.
    fun foveateEfbSetting() = vrBooleanSetting("FoveateEFB", false)

    fun virtualScreenSetting() = vrBooleanSetting("VirtualScreen", false)

    fun screenDistanceSetting() = vrFloatSetting("ScreenDistance", 1.5f)

    fun screenSizeSetting() = vrFloatSetting("ScreenSize", 1.5f)

    fun headLockedCurvatureSetting() = vrFloatSetting("HeadLockedCurvature", 0.0f)

    fun dontClearScreenSetting() = vrBooleanSetting("DontClearScreen", false)

    fun disableCpuCullSetting() = vrBooleanSetting("DisableCPUCull", false)

    fun clearEfbCopiesSetting() = vrIntSetting("ClearEFBCopies", 0)

    fun loadCustomShadersSetting() = vrBooleanSetting("LoadCustomShaders", false)


    private fun openXrRuntimeSetting() = vrBooleanSetting("EnableOpenXR", false)

    private fun perfDefaultsAppliedSetting() = androidBooleanSetting("QuestPerfProfileApplied", false)

    private fun backendMultithreadingReenabledSetting() =
        androidBooleanSetting("QuestBackendMultithreadingReenabled", false)

    private fun controllerSetupAskedSetting() =
        androidBooleanSetting("QuestControllerSetupAsked", false)

    fun shouldShowMirrorSurface(): Boolean {
        if (!BuildConfig.IS_QUEST) {
            return true
        }

        // Flat-in-VR is still an immersive OpenXR session (no 2D Android surface), so key the
        // mirror surface off whether we launch immersively, not off stereo vs. flat.
        return showMirrorSurfaceSetting().boolean || !isOpenXrImmersiveEnabled()
    }

    // True when we launch an immersive OpenXR session at all (either stereo 3D or flat panel).
    fun isOpenXrImmersiveEnabled(): Boolean {
        return BuildConfig.IS_QUEST && openXrEnabledSetting().boolean
    }

    fun isLaunchInVrEnabled(): Boolean {
        return BuildConfig.IS_QUEST &&
            openXrEnabledSetting().boolean &&
            launchInVrSetting().boolean
    }

    fun applyRecommendedDefaults(settings: Settings) {
        if (!BuildConfig.IS_QUEST) {
            return
        }

        StringSetting.MAIN_GFX_BACKEND.setString(settings, "Vulkan")
        BooleanSetting.GFX_BACKEND_MULTITHREADING.setBoolean(settings, true)
        IntSetting.GFX_EFB_SCALE.setInt(settings, 4)
        BooleanSetting.GFX_WAIT_FOR_SHADERS_BEFORE_STARTING.setBoolean(settings, false)
        BooleanSetting.MAIN_SHOW_INPUT_OVERLAY.setBoolean(settings, false)
        exactScreenDepthSetting().setBoolean(settings, true)
        androidDirectToHmdSetting().setBoolean(settings, true)
        removeBarsSetting().setBoolean(settings, true)
        virtualScreenSetting().setBoolean(settings, false)
        BooleanSetting.GFX_HACK_IMMEDIATE_XFB.setBoolean(settings, true)
        BooleanSetting.GFX_HACK_VI_SKIP.setBoolean(settings, false)
        perfDefaultsAppliedSetting().setBoolean(settings, true)
        backendMultithreadingReenabledSetting().setBoolean(settings, true)
    }

    fun shouldAskAboutControllerSetup(): Boolean {
        return BuildConfig.IS_QUEST && !controllerSetupAskedSetting().boolean
    }

    fun recordControllerSetupChoice(settings: Settings, applyDefaults: Boolean) {
        if (!BuildConfig.IS_QUEST) {
            return
        }

        if (applyDefaults) {
            applyDefaultControllerSetup(settings)
        }
        controllerSetupAskedSetting().setBoolean(settings, true)
    }

    /**
     * Installs the stock Quest mappings without making the user choose between Wii and GameCube.
     * The appropriate controller will be used automatically for each emulated console.
     */
    fun applyDefaultControllerSetup(settings: Settings) {
        if (!BuildConfig.IS_QUEST) {
            return
        }

        val gcPad = EmulatedController.getGcPad(0)
        gcPad.loadProfile(gcPad.getSysProfileDirectoryPath() + GC_PROFILE_NAME)
        gcPad.setDefaultDevice(OPENXR_DEVICE)

        val wiimote = EmulatedController.getWiimote(0)
        wiimote.loadProfile(wiimote.getSysProfileDirectoryPath() + WIIMOTE_PROFILE_NAME)
        wiimote.setDefaultDevice(OPENXR_DEVICE)

        val hotkeys = EmulatedController.getHotkeys()
        hotkeys.loadProfile(hotkeys.getSysProfileDirectoryPath() + HOTKEY_PROFILE_NAME)
        hotkeys.setDefaultDevice(OPENXR_DEVICE)

        // 6 is an emulated Standard Controller and 3 is an OpenXR Wii Remote.
        IntSetting.MAIN_SI_DEVICE_0.setInt(settings, 6)
        IntSetting.WIIMOTE_1_SOURCE.setInt(settings, 3)
    }

    fun prepareLaunchSettings(settings: Settings) {
        if (!BuildConfig.IS_QUEST) {
            return
        }

        if (!perfDefaultsAppliedSetting().boolean) {
            applyRecommendedDefaults(settings)
        }

        StringSetting.MAIN_GFX_BACKEND.setString(settings, "Vulkan")

        // The OpenXR immersive session runs whenever the runtime is enabled. "Launch games in VR"
        // then chooses stereo 3D (StereoMode::OpenXR) vs. a flat mono panel in the VR scene.
        val immersive = isOpenXrImmersiveEnabled()
        val stereo = isLaunchInVrEnabled()
        openXrRuntimeSetting().setBoolean(settings, immersive)
        flatScreenSetting().setBoolean(settings, immersive && !stereo)

        if (immersive) {
            // Native side maps EnableOpenXR + FlatScreen to the stereo mode; keep GFX_STEREO_MODE
            // set to OpenXR only for the stereo path so a stale value can't force stereo in flat.
            IntSetting.GFX_STEREO_MODE.setInt(settings, if (stereo) STEREO_MODE_OPENXR else 0)
            if (!backendMultithreadingReenabledSetting().boolean) {
                BooleanSetting.GFX_BACKEND_MULTITHREADING.setBoolean(settings, true)
                backendMultithreadingReenabledSetting().setBoolean(settings, true)
            }
            BooleanSetting.MAIN_SHOW_INPUT_OVERLAY.setBoolean(settings, false)
            // Immediately Present XFB is left as the user set it (applyRecommendedDefaults
            // turns it on): games that need it off now render correctly in VR, and the
            // head-pose lock is applied automatically whenever it is off.
            BooleanSetting.GFX_HACK_VI_SKIP.setBoolean(settings, false)
        } else {
            // flatScreen already set false above (immersive is false here).
            if (IntSetting.GFX_STEREO_MODE.int == STEREO_MODE_OPENXR) {
                IntSetting.GFX_STEREO_MODE.setInt(settings, 0)
            }
        }

        // Controller profiles are configured explicitly by the first-launch prompt or settings UI.
        // Do not overwrite a user's controller selection whenever a game starts.
    }
}
