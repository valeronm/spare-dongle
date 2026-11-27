package io.github.valeronm.sparedongle.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Block
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import io.github.valeronm.sparedongle.MessageKind
import io.github.valeronm.sparedongle.ProtocolCode
import io.github.valeronm.sparedongle.R
import io.github.valeronm.sparedongle.Side

private val BadgeSize = 96.dp
private val MessageIconSize = 48.dp

private const val FREE_HEIGHT_ABOVE = 0.4f

internal enum class Retry(@param:StringRes val label: Int) {
    AskAgain(R.string.ask_again),
    TryAgain(R.string.try_again),
}

internal sealed interface Detail {
    data class Hint(@param:StringRes val text: Int) : Detail
    data class Reason(val text: String) : Detail
    data class Versions(val dongle: Int, val app: Int) : Detail
}

/** A null [icon] shows progress. */
internal data class Message(
    @param:StringRes val headline: Int,
    val icon: ImageVector?,
    val failed: Boolean,
    val detail: Detail?,
    val retry: Retry?,
    val headlineArg: ProtocolCode? = null,
)

internal fun message(kind: MessageKind): Message = when (kind) {
    MessageKind.PlugIn -> Message(R.string.plug_in, Icons.Rounded.Usb, false, Detail.Hint(R.string.plug_in_hint), null)
    MessageKind.Waiting -> Message(R.string.waiting, null, false, null, null)
    MessageKind.Denied ->
        Message(R.string.access_denied, Icons.Rounded.Block, false, Detail.Hint(R.string.denied_hint), Retry.AskAgain)
    MessageKind.CannotOpen ->
        Message(R.string.cannot_open, Icons.Rounded.ErrorOutline, true, Detail.Hint(R.string.cannot_open_hint), Retry.TryAgain)
    is MessageKind.ReadFailed ->
        Message(R.string.read_failed, Icons.Rounded.ErrorOutline, true, Detail.Reason(kind.reason), Retry.TryAgain)
    is MessageKind.Outdated -> Message(
        when (kind.mismatch.older) {
            Side.App -> R.string.update_app
            Side.Dongle -> R.string.update_firmware
        },
        Icons.Rounded.ErrorOutline,
        true,
        Detail.Versions(kind.mismatch.dongle, kind.mismatch.app),
        null,
    )
    MessageKind.NotBack -> Message(R.string.not_back, Icons.Rounded.ErrorOutline, true, null, Retry.TryAgain)
}

internal fun switchingMessage(target: ProtocolCode, askingAccess: Boolean): Message {
    val detail = Detail.Hint(if (askingAccess) R.string.allow_access else R.string.switching_hint)
    return Message(R.string.switching_to, null, false, detail, null, headlineArg = target)
}

@Composable
internal fun MessageScreen(message: Message, onAskAgain: () -> Unit, onTryAgain: () -> Unit) {
    val onRetry = when (message.retry) {
        Retry.AskAgain -> onAskAgain
        Retry.TryAgain -> onTryAgain
        null -> null
    }
    Scaffold(topBar = { TopAppBar(title = { Text(stringResource(R.string.app_name)) }) }) { padding ->
        BoxWithConstraints(Modifier.fillMaxSize().padding(padding)) {
            MessageColumn(message, onRetry, maxHeight)
        }
    }
}

@Composable
private fun MessageColumn(message: Message, onRetry: (() -> Unit)?, viewport: Dp) {
    // Under verticalScroll a Column's weights share its minimum height.
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .heightIn(min = viewport)
            .padding(horizontal = 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.weight(FREE_HEIGHT_ABOVE))
        Badge(message)
        Text(
            message.headlineArg?.let { stringResource(message.headline, protocolName(it)) }
                ?: stringResource(message.headline),
            style = MaterialTheme.typography.headlineSmall,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        when (val detail = message.detail) {
            null -> Unit
            is Detail.Hint -> DetailText(stringResource(detail.text))
            is Detail.Reason -> SelectionContainer { DetailText(detail.text, MaterialTheme.typography.bodySmall) }
            is Detail.Versions -> DetailText(latinString(R.string.version_mismatch, detail.dongle, detail.app))
        }
        val retry = message.retry
        if (retry != null && onRetry != null) {
            FilledTonalButton(onClick = onRetry) { Text(stringResource(retry.label)) }
        }
        Spacer(Modifier.weight(1f - FREE_HEIGHT_ABOVE))
    }
}

@Composable
private fun DetailText(text: String, style: TextStyle = MaterialTheme.typography.bodyMedium) {
    Text(
        text,
        style = style,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        textAlign = TextAlign.Center,
        modifier = Modifier.widthIn(max = 320.dp),
    )
}

@Composable
private fun Badge(message: Message) {
    val colors = MaterialTheme.colorScheme
    IconCircle(
        container = if (message.failed) colors.errorContainer else colors.surfaceContainerHighest,
        content = if (message.failed) colors.onErrorContainer else colors.onSurfaceVariant,
        size = BadgeSize,
    ) {
        if (message.icon == null) {
            CircularProgressIndicator(Modifier.size(MessageIconSize))
        } else {
            Icon(message.icon, contentDescription = null, modifier = Modifier.size(MessageIconSize))
        }
    }
}
