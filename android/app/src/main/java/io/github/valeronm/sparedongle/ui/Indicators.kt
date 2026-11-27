package io.github.valeronm.sparedongle.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.BatteryUnknown
import androidx.compose.material.icons.rounded.Battery0Bar
import androidx.compose.material.icons.rounded.Battery1Bar
import androidx.compose.material.icons.rounded.Battery2Bar
import androidx.compose.material.icons.rounded.Battery3Bar
import androidx.compose.material.icons.rounded.Battery4Bar
import androidx.compose.material.icons.rounded.Battery5Bar
import androidx.compose.material.icons.rounded.Battery6Bar
import androidx.compose.material.icons.rounded.BatteryAlert
import androidx.compose.material.icons.rounded.BatteryChargingFull
import androidx.compose.material.icons.rounded.BatteryFull
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Error
import androidx.compose.material.icons.rounded.LinkOff
import androidx.compose.material.icons.rounded.PowerSettingsNew
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import io.github.valeronm.sparedongle.BatteryLevel
import io.github.valeronm.sparedongle.Charge
import io.github.valeronm.sparedongle.LinkCode
import io.github.valeronm.sparedongle.Signal
import io.github.valeronm.sparedongle.SignalQuality

@Immutable
data class StateStyle(val icon: ImageVector, val container: Color, val onContainer: Color, val accent: Color)

private const val IDLE_ICON_ALPHA = 0.6f

@Composable
fun stateStyle(code: LinkCode): StateStyle {
    val c = MaterialTheme.colorScheme
    return when (code) {
        LinkCode.Connected -> StateStyle(Icons.Rounded.CheckCircle, c.primary, c.onPrimary, c.primary)
        LinkCode.Standby -> StateStyle(Icons.Rounded.CheckCircle, c.primaryContainer, c.onPrimaryContainer, c.primary)
        LinkCode.Waiting -> StateStyle(
            Icons.Rounded.PowerSettingsNew,
            c.surfaceContainerHighest,
            c.onSurfaceVariant.copy(alpha = IDLE_ICON_ALPHA),
            c.onSurfaceVariant,
        )
        LinkCode.NotPaired -> StateStyle(
            Icons.Rounded.LinkOff,
            c.surfaceContainerHighest,
            c.onSurfaceVariant.copy(alpha = IDLE_ICON_ALPHA),
            c.onSurfaceVariant,
        )
        LinkCode.RadioFailed -> StateStyle(Icons.Rounded.Error, c.errorContainer, c.onErrorContainer, c.error)
    }
}

private val StateColorSpec = tween<Color>(Motion.MEDIUM2, easing = Motion.Standard)

@Composable
fun animatedStateStyle(code: LinkCode): StateStyle {
    val target = stateStyle(code)
    val container by animateColorAsState(target.container, StateColorSpec, label = "container")
    val onContainer by animateColorAsState(target.onContainer, StateColorSpec, label = "onContainer")
    val accent by animateColorAsState(target.accent, StateColorSpec, label = "accent")
    return StateStyle(target.icon, container, onContainer, accent)
}

@Composable
fun signalColor(quality: SignalQuality): Color {
    val c = MaterialTheme.colorScheme
    return when (quality) {
        SignalQuality.Good, SignalQuality.Fair -> c.onSurfaceVariant
        SignalQuality.Weak -> c.error
    }
}

private val BarHeights = listOf(6.dp, 10.dp, 14.dp, 18.dp)
private val BarWidth = 3.dp
private val BarGap = 2.dp
val SignalBarsWidth = BarWidth * BarHeights.size + BarGap * (BarHeights.size - 1)

@Composable
fun SignalBars(signal: Signal?, modifier: Modifier = Modifier) {
    val empty = MaterialTheme.colorScheme.outlineVariant
    val filled = signal?.let { signalColor(it.quality) } ?: empty
    val count = signal?.bars ?: 0
    Canvas(modifier.size(width = SignalBarsWidth, height = BarHeights.last())) {
        val width = BarWidth.toPx()
        val radius = CornerRadius(width / 2)
        BarHeights.forEachIndexed { index, height ->
            val barHeight = height.toPx()
            drawRoundRect(
                color = if (index < count) filled else empty,
                topLeft = Offset(index * (width + BarGap.toPx()), size.height - barHeight),
                size = Size(width, barHeight),
                cornerRadius = radius,
            )
        }
    }
}

@Immutable
data class BatteryStyle(val icon: ImageVector, val tint: Color)

private val BatteryBars = listOf(
    Icons.Rounded.Battery0Bar,
    Icons.Rounded.Battery1Bar,
    Icons.Rounded.Battery2Bar,
    Icons.Rounded.Battery3Bar,
    Icons.Rounded.Battery4Bar,
    Icons.Rounded.Battery5Bar,
    Icons.Rounded.Battery6Bar,
    Icons.Rounded.BatteryFull,
)

@Composable
fun batteryStyle(level: BatteryLevel?, charge: Charge?): BatteryStyle? {
    val c = MaterialTheme.colorScheme
    return when (charge) {
        Charge.Fault -> BatteryStyle(Icons.Rounded.BatteryAlert, c.error)
        Charge.Charging -> BatteryStyle(Icons.Rounded.BatteryChargingFull, c.primary)
        Charge.Charged -> BatteryStyle(Icons.Rounded.BatteryFull, c.primary)
        Charge.Discharging -> BatteryStyle(Icons.AutoMirrored.Rounded.BatteryUnknown, c.onSurfaceVariant)
        null -> level?.let {
            BatteryStyle(BatteryBars[batteryBarIndex(it.percent, BatteryBars.size)], if (it.low) c.error else c.onSurfaceVariant)
        }
    }
}
