package io.github.valeronm.sparedongle.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.ListItemColors
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.ListItemShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

@Composable
fun SectionHeader(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
        modifier = modifier
            .padding(start = 4.dp, top = 24.dp, bottom = 8.dp)
            .semantics { heading() },
    )
}

@Immutable
class Segment(private val index: Int, private val count: Int) {
    val gap: Modifier
        get() = if (index < count - 1) Modifier.padding(bottom = ListItemDefaults.SegmentedGap) else Modifier

    val shapes: ListItemShapes
        @Composable get() = ListItemDefaults.segmentedShapes(index, count)

    // The stock segmented container is the surface color, the same as the screen behind the group.
    val colors: ListItemColors
        @Composable get() {
            val defaults = ListItemDefaults.segmentedColors()
            val container = MaterialTheme.colorScheme.surfaceContainer
            return remember(defaults, container) {
                defaults.copy(containerColor = container, disabledContainerColor = container)
            }
        }
}

fun <T : Any> LazyListScope.segmented(
    items: List<T>,
    key: (T) -> Any,
    content: @Composable LazyItemScope.(item: T, segment: Segment) -> Unit,
) {
    itemsIndexed(items, key = { _, item -> key(item) }) { index, item -> content(item, Segment(index, items.size)) }
}

@Composable
fun IconCircle(container: Color, content: Color, size: Dp, icon: @Composable BoxScope.() -> Unit) {
    Surface(shape = CircleShape, color = container, contentColor = content, modifier = Modifier.size(size)) {
        Box(contentAlignment = Alignment.Center, content = icon)
    }
}
