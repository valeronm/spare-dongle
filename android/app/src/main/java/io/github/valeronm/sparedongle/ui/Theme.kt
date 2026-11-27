package io.github.valeronm.sparedongle.ui

import android.os.Build
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle

// Tonal-spot palettes from seed #2B6A8F at the M3 baseline tones, for devices without dynamic color.
private val LightColors = lightColorScheme(
    primary = Color(0xFF246488),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFC8E6FF),
    onPrimaryContainer = Color(0xFF001E2E),
    inversePrimary = Color(0xFF94CDF6),
    secondary = Color(0xFF4F606E),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFD3E5F5),
    onSecondaryContainer = Color(0xFF0B1D29),
    tertiary = Color(0xFF63597C),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFE9DDFF),
    onTertiaryContainer = Color(0xFF1F1635),
    error = Color(0xFFBA1A1A),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
    background = Color(0xFFF6F9FE),
    onBackground = Color(0xFF181C20),
    surface = Color(0xFFF6F9FE),
    onSurface = Color(0xFF181C20),
    surfaceVariant = Color(0xFFDDE3EA),
    onSurfaceVariant = Color(0xFF41484D),
    surfaceTint = Color(0xFF246488),
    inverseSurface = Color(0xFF2D3135),
    inverseOnSurface = Color(0xFFEEF1F6),
    outline = Color(0xFF71787E),
    outlineVariant = Color(0xFFC1C7CE),
    scrim = Color(0xFF000000),
    surfaceBright = Color(0xFFF6F9FE),
    surfaceDim = Color(0xFFD7DADF),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFF1F4F9),
    surfaceContainer = Color(0xFFEBEEF3),
    surfaceContainerHigh = Color(0xFFE5E8ED),
    surfaceContainerHighest = Color(0xFFDFE3E8),
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF94CDF6),
    onPrimary = Color(0xFF00344D),
    primaryContainer = Color(0xFF004C6D),
    onPrimaryContainer = Color(0xFFC8E6FF),
    inversePrimary = Color(0xFF246488),
    secondary = Color(0xFFB7C9D8),
    onSecondary = Color(0xFF21323F),
    secondaryContainer = Color(0xFF384956),
    onSecondaryContainer = Color(0xFFD3E5F5),
    tertiary = Color(0xFFCEC0E8),
    onTertiary = Color(0xFF342B4B),
    tertiaryContainer = Color(0xFF4B4163),
    onTertiaryContainer = Color(0xFFE9DDFF),
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF101417),
    onBackground = Color(0xFFDFE3E8),
    surface = Color(0xFF101417),
    onSurface = Color(0xFFDFE3E8),
    surfaceVariant = Color(0xFF41484D),
    onSurfaceVariant = Color(0xFFC1C7CE),
    surfaceTint = Color(0xFF94CDF6),
    inverseSurface = Color(0xFFDFE3E8),
    inverseOnSurface = Color(0xFF2D3135),
    outline = Color(0xFF8B9198),
    outlineVariant = Color(0xFF41484D),
    scrim = Color(0xFF000000),
    surfaceBright = Color(0xFF353A3E),
    surfaceDim = Color(0xFF101417),
    surfaceContainerLowest = Color(0xFF0A0F12),
    surfaceContainerLow = Color(0xFF181C20),
    surfaceContainer = Color(0xFF1C2024),
    surfaceContainerHigh = Color(0xFF262A2E),
    surfaceContainerHighest = Color(0xFF313539),
)

@Composable
fun SpareDongleTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val context = LocalContext.current
    val colors = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        dark -> DarkColors
        else -> LightColors
    }
    MaterialTheme(colorScheme = colors, content = content)
}

val TextStyle.numeric: TextStyle get() = copy(fontFeatureSettings = "tnum")

object Motion {
    val Standard = CubicBezierEasing(0.2f, 0f, 0f, 1f)
    val EmphasizedDecelerate = CubicBezierEasing(0.05f, 0.7f, 0.1f, 1f)
    val EmphasizedAccelerate = CubicBezierEasing(0.3f, 0f, 0.8f, 0.15f)
    const val SHORT4 = 200
    const val MEDIUM2 = 300
    const val MEDIUM4 = 400
}
