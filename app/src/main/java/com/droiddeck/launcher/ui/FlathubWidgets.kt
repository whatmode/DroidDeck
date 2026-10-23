package com.droiddeck.launcher.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.hoverable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droiddeck.launcher.R

@Composable
internal fun ModeHeader(title: String, query: String, onQuery: (String) -> Unit, placeholder: String, trailing: @Composable () -> Unit) {
    val colors = MaterialTheme.colorScheme
    val pal = LocalPalette.current
    var searching by remember { mutableStateOf(query.isNotEmpty()) }
    val open by animateFloatAsState(if (searching) 1f else 0f, Motion.sp(0.75f, 420f), label = "searchOpen")
    Column(Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp).padding(vertical = 4.dp)) {
            Text(
                title, fontSize = 15.sp, fontWeight = FontWeight.Bold, color = colors.onBackground, maxLines = 1,
                modifier = Modifier.padding(start = 4.dp, end = 4.dp),
            )
            BoxWithConstraints(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                if (searching || open > 0.01f) {
                    val w = androidx.compose.ui.unit.lerp(44.dp, maxWidth.coerceAtMost(520.dp), open)
                    Box(Modifier.width(w).graphicsLayer { alpha = (open * 1.6f).coerceAtMost(1f) }) {
                        AdbTextField(
                            value = query, onValueChange = onQuery, label = "", keyboardType = KeyboardType.Text, imeAction = ImeAction.Search,
                            placeholder = placeholder, compact = true, focusOnShow = true,
                            onFocusChange = { focused -> if (!focused && query.isEmpty()) searching = false },
                        )
                    }
                } else SearchIcon(stringResource(R.string.stores_search_open)) { searching = true }
            }
            trailing()
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(pal.line))
    }
}

internal fun flathubPalette(base: Palette): Palette = Palette(
    base.id, base.label, base.detail, base.background, base.surface, base.surfaceVariant, base.line, base.line2,
    base.onBackground, base.onSurfaceVariant, primary = FlathubTeal, primary2 = Color(0xFF2E958D), onPrimary = FlathubInk, signal = FlathubTeal,
    good = base.good, error = base.error,
)

@Composable
internal fun FlathubTheme(content: @Composable () -> Unit) {
    val base = LocalPalette.current
    val p = remember(base) { flathubPalette(base) }
    val scheme = MaterialTheme.colorScheme.copy(primary = p.primary, onPrimary = p.onPrimary)
    CompositionLocalProvider(LocalPalette provides p) { MaterialTheme(colorScheme = scheme, typography = MaterialTheme.typography, content = content) }
}

@Composable
internal fun SearchIcon(description: String, onClick: () -> Unit) {
    val colors = MaterialTheme.colorScheme
    val pal = LocalPalette.current
    val src = remember { MutableInteractionSource() }
    val hot = rememberHot(src)
    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier.paneItem("storesearch").size(40.dp).clip(CircleShape).background(if (hot) pal.signal.copy(alpha = 0.14f) else Color.Transparent)
            .glideBorder(hot, CircleShape, pal.signal)
            .hoverable(src).clickable(interactionSource = src, indication = LocalIndication.current, role = Role.Button, onClick = onClick)
            .controllerConfirm(onClick = onClick),
    ) { Icon(Icons.Filled.Search, description, tint = if (hot) pal.signal else colors.onSurfaceVariant, modifier = Modifier.size(20.dp)) }
}

