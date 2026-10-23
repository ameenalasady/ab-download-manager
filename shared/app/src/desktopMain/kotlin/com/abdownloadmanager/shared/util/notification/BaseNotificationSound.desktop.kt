package com.abdownloadmanager.shared.util.notification

import ir.amirab.util.execAndForget
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import org.koin.core.component.KoinComponent
import org.koin.core.component.inject
import java.awt.Toolkit
import java.io.File
import javax.sound.sampled.AudioInputStream
import javax.sound.sampled.AudioSystem
import javax.sound.sampled.LineEvent

actual fun platformNotificationSound(): INotificationSound = DesktopNotificationSound

internal object DesktopNotificationSound : BaseNotificationSound(), KoinComponent {
    private val notificationSoundSettings: INotificationSettingsStorage by inject()

    override fun getNotificationSoundSettingsStorage(): INotificationSettingsStorage {
        return notificationSoundSettings
    }

    override fun actualPlay(audioSource: String) {
        if (playChosenSound(audioSource)) return
        if (playDesktopThemeSound()) return
        runCatching {
            Toolkit.getDefaultToolkit().beep()
        }
    }

    private fun playChosenSound(audioSource: String): Boolean {
        val audioStream = getAudioSource(audioSource) ?: return false
        return runCatching {
            audioStream.use {
                val clip = AudioSystem.getClip()
                clip.open(it)
                clip.addLineListener { event ->
                    if (event.type == LineEvent.Type.STOP) {
                        clip.close()
                    }
                }
                clip.start()
            }
            true
        }.getOrElse {
            // java can not always open a line on linux, where the sound server usually only
            // reaches it through an alsa compatibility device that may not be there at all
            false
        }
    }

    /**
     * The X11 bell [Toolkit.beep] rings is turned off on every modern linux desktop and does not
     * exist under wayland, so the sound daemon is asked for the theme sound instead.
     */
    private fun playDesktopThemeSound(): Boolean {
        if (!Platform.isLinux()) return false
        if (execAndForget(arrayOf("canberra-gtk-play", "-i", "message"), settleFor = 150)) {
            return true
        }
        val themeSound = listOf(
            "/usr/share/sounds/freedesktop/stereo/message.oga",
            "/usr/share/sounds/freedesktop/stereo/complete.oga",
        ).firstOrNull { File(it).isFile } ?: return false
        return execAndForget(arrayOf("paplay", themeSound), settleFor = 150)
    }

    private fun getAudioSource(audioSource: String): AudioInputStream? {
        if (audioSource.isBlank()) {
            return null
        }
        return runCatching {
            AudioSystem.getAudioInputStream(File(audioSource))
        }.getOrNull()
    }
}
