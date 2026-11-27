package io.github.valeronm.sparedongle.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.plus
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import io.github.valeronm.sparedongle.BuildConfig
import io.github.valeronm.sparedongle.CounterValue
import io.github.valeronm.sparedongle.DongleModel
import io.github.valeronm.sparedongle.R
import kotlinx.coroutines.launch

private sealed interface DetailSegment {
    val key: Any

    data class Row(@param:StringRes val label: Int, val value: String, val wraps: Boolean = false) : DetailSegment {
        override val key: Any get() = label
    }

    data class Counters(val counters: List<CounterValue>) : DetailSegment {
        override val key: Any get() = "counters"
    }
}

private data class DetailSection(@param:StringRes val title: Int, val segments: List<DetailSegment>)

@Composable
private fun detailSections(dongle: DongleModel): List<DetailSection> = listOf(
    DetailSection(
        R.string.section_versions,
        listOf(
            DetailSegment.Row(R.string.row_firmware, versionText(dongle.firmware)),
            DetailSegment.Row(R.string.row_bootloader, bootloaderText(dongle.bootloader)),
            DetailSegment.Row(R.string.row_softdevice, softdeviceText(dongle.softdevice)),
            DetailSegment.Row(R.string.row_app, BuildConfig.VERSION_NAME),
        ),
    ),
    DetailSection(
        R.string.section_runtime,
        listOf(
            DetailSegment.Row(R.string.row_uptime, uptimeText(dongle.uptimeMs)),
            DetailSegment.Row(R.string.row_reset, resetCausesText(dongle.resetCauses), wraps = true),
        ),
    ),
    DetailSection(
        R.string.section_diagnostics,
        listOfNotNull(
            dongle.counters.takeIf { it.isNotEmpty() }?.let(DetailSegment::Counters),
            DetailSegment.Row(R.string.row_history, historyText(dongle.history), wraps = true),
        ),
    ),
)

/** [onShare] reads the report and shares it, or calls its argument on the main thread when it can't be read. */
@Composable
fun DetailsScreen(dongle: DongleModel, onBack: () -> Unit, onShare: (onFailed: () -> Unit) -> Unit) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val snackbar = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val reportFailed = stringResource(R.string.report_failed)
    val sections = detailSections(dongle)
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            LargeTopAppBar(
                title = { Text(stringResource(R.string.details)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = stringResource(R.string.back))
                    }
                },
                actions = {
                    IconButton(onClick = { onShare { scope.launch { snackbar.showSnackbar(reportFailed) } } }) {
                        Icon(Icons.Rounded.Share, contentDescription = stringResource(R.string.share_report))
                    }
                },
                scrollBehavior = scrollBehavior,
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = padding + PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
        ) {
            for (section in sections) {
                item(key = section.title) { SectionHeader(stringResource(section.title)) }
                segmented(section.segments, key = { it.key }) { item, segment ->
                    when (item) {
                        is DetailSegment.Row -> Detail(item, segment)
                        is DetailSegment.Counters -> Counters(item.counters, segment)
                    }
                }
            }
        }
    }
}

@Composable
private fun Detail(row: DetailSegment.Row, segment: Segment) {
    val value: @Composable () -> Unit = {
        Text(
            row.value,
            style = MaterialTheme.typography.bodyLarge.numeric,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    ListItem(
        modifier = segment.gap.semantics(mergeDescendants = true) {},
        shapes = segment.shapes,
        colors = segment.colors,
        supportingContent = if (row.wraps) value else null,
        trailingContent = if (row.wraps) null else value,
    ) {
        Text(stringResource(row.label), style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurface)
    }
}

@Composable
private fun Counters(counters: List<CounterValue>, segment: Segment) {
    ListItem(
        modifier = segment.gap,
        shapes = segment.shapes,
        colors = segment.colors,
        contentPadding = PaddingValues(16.dp),
    ) {
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            for (counter in counters) {
                val name = counterName(counter.key)
                val value = countText(counter.value)
                val description = stringResource(R.string.counter_description, name, value)
                Column(Modifier.clearAndSetSemantics { contentDescription = description }) {
                    Text(
                        value,
                        style = MaterialTheme.typography.titleLarge.numeric,
                        color = if (counter.value == 0L) {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        } else {
                            MaterialTheme.colorScheme.onSurface
                        },
                    )
                    Text(
                        name,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}
