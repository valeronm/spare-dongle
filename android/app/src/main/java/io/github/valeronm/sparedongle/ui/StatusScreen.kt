package io.github.valeronm.sparedongle.ui

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.plus
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.ChevronRight
import androidx.compose.material.icons.rounded.Keyboard
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Mouse
import androidx.compose.material.icons.rounded.Sensors
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.max
import io.github.valeronm.sparedongle.DeviceKind
import io.github.valeronm.sparedongle.DeviceStatus
import io.github.valeronm.sparedongle.DongleModel
import io.github.valeronm.sparedongle.LinkCode
import io.github.valeronm.sparedongle.Notice
import io.github.valeronm.sparedongle.ProtocolCode
import io.github.valeronm.sparedongle.R
import io.github.valeronm.sparedongle.Signal
import io.github.valeronm.sparedongle.StatusModel
import io.github.valeronm.sparedongle.Usb
import io.github.valeronm.sparedongle.switchTargets

private const val LARGE_FONT_SCALE = 1.5f
private val TrailingMinWidth = 56.dp
private val TrailingGap = 4.dp
private val AvatarSize = 40.dp

private enum class DongleRow { Protocol, Details }

/** [notice] is announced once; [onNoticeShown] gets it when its announcement ends or leaves the screen. */
@Composable
fun StatusScreen(
    status: StatusModel,
    dongle: DongleModel,
    onDetails: () -> Unit,
    onSwitch: (ProtocolCode) -> Unit,
    notice: Notice?,
    onNoticeShown: (Notice) -> Unit,
) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    val banner = bannerOf(status)
    val trailingWidth = trailingWidth()
    val snackbar = remember { SnackbarHostState() }
    var choosing by rememberSaveable { mutableStateOf(false) }
    val targets = switchTargets(status, dongle)
    if (notice != null) {
        val text = noticeText(notice)
        LaunchedEffect(notice) {
            try {
                snackbar.showSnackbar(text)
            } finally {
                onNoticeShown(notice)
            }
        }
    }
    if (choosing) {
        SwitchDialog(
            targets,
            onSwitch = { target ->
                choosing = false
                onSwitch(target)
            },
            onDismiss = { choosing = false },
        )
    }
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            TopAppBar(title = { Text(stringResource(R.string.app_name)) }, scrollBehavior = scrollBehavior)
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = padding + PaddingValues(start = 16.dp, top = 8.dp, end = 16.dp, bottom = 24.dp),
        ) {
            if (banner != null) {
                item(key = "banner") { Banner(banner, Modifier.animateItem()) }
            }
            item(key = "devices-header") { SectionHeader(stringResource(R.string.devices), Modifier.animateItem()) }
            segmented(status.devices, key = { it.slot }) { device, segment ->
                DeviceRow(
                    device,
                    segment,
                    trailingWidth,
                    Modifier
                        .animateItem(
                            fadeInSpec = tween(Motion.MEDIUM2),
                            placementSpec = spring(stiffness = Spring.StiffnessMediumLow),
                            fadeOutSpec = tween(Motion.SHORT4),
                        )
                        .then(segment.gap),
                )
            }
            item(key = "dongle-header") { SectionHeader(stringResource(R.string.dongle), Modifier.animateItem()) }
            segmented(DongleRow.entries, key = { it }) { row, segment ->
                val modifier = Modifier.animateItem().then(segment.gap)
                when (row) {
                    DongleRow.Protocol ->
                        ProtocolRow(
                            protocol = protocolName(status.protocol),
                            icon = protocolIcon(status.protocol),
                            canSwitch = targets.isNotEmpty(),
                            onSwitch = { choosing = true },
                            segment = segment,
                            modifier = modifier,
                        )
                    DongleRow.Details -> DetailsRow(dongle, onDetails, segment, modifier)
                }
            }
        }
    }
}

private data class BannerContent(val radioFailed: Boolean, val usb: Usb?)

private fun bannerOf(status: StatusModel): BannerContent? {
    val radioFailed = status.link == LinkCode.RadioFailed
    val usb = status.usb.takeIf { it != Usb.Live }
    return if (radioFailed || usb != null) BannerContent(radioFailed, usb) else null
}

@Composable
private fun Banner(content: BannerContent, modifier: Modifier = Modifier) {
    val style = stateStyle(LinkCode.RadioFailed)
    Card(
        modifier = modifier
            .fillMaxWidth()
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(containerColor = style.container, contentColor = style.onContainer),
    ) {
        Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Icon(if (content.radioFailed) style.icon else Icons.Rounded.Warning, contentDescription = null)
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                if (content.radioFailed) {
                    Text(stateName(LinkCode.RadioFailed), style = MaterialTheme.typography.titleMedium)
                    Text(stringResource(R.string.radio_failed_hint), style = MaterialTheme.typography.bodyMedium)
                }
                if (content.usb != null) {
                    Text(
                        usbName(content.usb),
                        style = if (content.radioFailed) MaterialTheme.typography.bodyMedium else MaterialTheme.typography.titleMedium,
                    )
                }
            }
        }
    }
}

@Composable
private fun Avatar(icon: ImageVector, container: Color, content: Color) {
    IconCircle(container, content, AvatarSize) { Icon(icon, contentDescription = null, modifier = Modifier.size(24.dp)) }
}

@Composable
private fun StateIcon(icon: ImageVector, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current) {
    Crossfade(icon, animationSpec = tween(Motion.SHORT4, easing = Motion.Standard), label = "state icon") {
        Icon(it, contentDescription = null, modifier = modifier, tint = tint)
    }
}

@Composable
private fun DeviceRow(device: DeviceStatus, segment: Segment, trailingWidth: Dp, modifier: Modifier = Modifier) {
    val style = animatedStateStyle(device.state)
    val kind = kindName(device.kind)
    val state = stateName(device.state)
    val battery = batteryStyle(device.batteryLevel, device.charge)
    val signal = device.signal
    val percent = device.batteryLevel?.let { percentText(it.percent) }
    val large = LocalDensity.current.fontScale >= LARGE_FONT_SCALE
    val charge = device.charge?.let { chargeSpoken(it) }
    val batteryReading = listOfNotNull(percent, charge).joinToString(", ").ifEmpty { null }
    val description = listOfNotNull(
        device.name,
        kind,
        state,
        signal?.let { stringResource(R.string.signal_description, qualityName(it.quality), dbmText(it)) },
        batteryReading?.let { stringResource(R.string.battery_description, it) },
    ).joinToString(", ")
    ListItem(
        modifier = modifier
            .animateContentSize(spring(stiffness = Spring.StiffnessMediumLow))
            .clearAndSetSemantics { contentDescription = description },
        shapes = segment.shapes,
        colors = segment.colors,
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
        leadingContent = { Avatar(kindIcon(device.kind), style.container, style.onContainer) },
        supportingContent = {
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    itemVerticalAlignment = Alignment.CenterVertically,
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        StateIcon(style.icon, Modifier.size(16.dp), tint = style.accent)
                        Text(state, style = MaterialTheme.typography.labelLarge, color = style.accent)
                    }
                    if (!large && signal != null) SignalDbm(signal)
                }
                if (large) Readings(signal, battery, percent)
            }
        },
        trailingContent = if (large) null else {
            { Trailing(signal, battery, percent, trailingWidth) }
        },
    ) {
        Text(
            device.name ?: kind,
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun trailingWidth(): Dp {
    val style = MaterialTheme.typography.labelLarge.numeric
    val measurer = rememberTextMeasurer()
    val widest = percentText(FULL_PERCENT)
    val widestPx = remember(widest, style, measurer) { measurer.measure(widest, style).size.width }
    return max(TrailingMinWidth, with(LocalDensity.current) { widestPx.toDp() } + TrailingGap + SignalBarsWidth)
}

@Composable
private fun Trailing(signal: Signal?, battery: BatteryStyle?, percent: String?, width: Dp) {
    Column(
        modifier = Modifier.width(width),
        horizontalAlignment = Alignment.End,
        verticalArrangement = Arrangement.spacedBy(TrailingGap),
    ) {
        SignalBars(signal)
        if (battery != null) BatteryReading(battery, percent, percentFirst = true)
    }
}

@Composable
private fun Readings(signal: Signal?, battery: BatteryStyle?, percent: String?) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
        itemVerticalAlignment = Alignment.CenterVertically,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(TrailingGap)) {
            SignalBars(signal)
            if (signal != null) SignalDbm(signal)
        }
        if (battery != null) BatteryReading(battery, percent, percentFirst = false)
    }
}

@Composable
private fun SignalDbm(signal: Signal) {
    Text(dbmText(signal), style = MaterialTheme.typography.labelLarge.numeric, color = signalColor(signal.quality))
}

@Composable
private fun BatteryReading(battery: BatteryStyle, percent: String?, percentFirst: Boolean) {
    val text: @Composable () -> Unit = {
        if (percent != null) Text(percent, style = MaterialTheme.typography.labelLarge.numeric, color = battery.tint)
    }
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(TrailingGap)) {
        if (percentFirst) text()
        Icon(battery.icon, contentDescription = null, modifier = Modifier.size(SignalBarsWidth), tint = battery.tint)
        if (!percentFirst) text()
    }
}

@Composable
private fun ProtocolRow(
    protocol: String,
    icon: ImageVector,
    canSwitch: Boolean,
    onSwitch: () -> Unit,
    segment: Segment,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    val label = stringResource(R.string.switch_title)
    ListItem(
        onClick = onSwitch,
        enabled = canSwitch,
        modifier = modifier.labeledButton(label, canSwitch, onSwitch),
        shapes = segment.shapes,
        colors = segment.colors,
        leadingContent = { Avatar(icon, colors.surfaceContainerHighest, colors.onSurfaceVariant) },
        supportingContent = { Text(protocol) },
        trailingContent = { Icon(Icons.Rounded.SwapHoriz, contentDescription = null) },
    ) {
        Text(stringResource(R.string.protocol))
    }
}

@Composable
private fun DetailsRow(dongle: DongleModel, onDetails: () -> Unit, segment: Segment, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    val label = stringResource(R.string.details)
    val summary = stringResource(R.string.dongle_summary, versionText(dongle.firmware), uptimeText(dongle.uptimeMs))
    ListItem(
        onClick = onDetails,
        modifier = modifier.labeledButton(label, enabled = true, onDetails),
        shapes = segment.shapes,
        colors = segment.colors,
        leadingContent = { Avatar(Icons.Rounded.Memory, colors.surfaceContainerHighest, colors.onSurfaceVariant) },
        supportingContent = { Text(summary, style = MaterialTheme.typography.bodyMedium.numeric) },
        trailingContent = { Icon(Icons.Rounded.ChevronRight, contentDescription = null) },
    ) {
        Text(label)
    }
}

// The clickable overload takes no click label, and this action replaces the unlabeled one it adds.
private fun Modifier.labeledButton(label: String, enabled: Boolean, action: () -> Unit): Modifier = semantics {
    role = Role.Button
    if (enabled) {
        onClick(label = label) {
            action()
            true
        }
    }
}

private fun protocolIcon(code: ProtocolCode): ImageVector = when (code) {
    ProtocolCode.Bluetooth -> Icons.Rounded.Bluetooth
    ProtocolCode.Nuphy, ProtocolCode.Unifying -> Icons.Rounded.Sensors
}

private fun kindIcon(kind: DeviceKind): ImageVector = when (kind) {
    DeviceKind.Keyboard -> Icons.Rounded.Keyboard
    DeviceKind.Mouse -> Icons.Rounded.Mouse
}
