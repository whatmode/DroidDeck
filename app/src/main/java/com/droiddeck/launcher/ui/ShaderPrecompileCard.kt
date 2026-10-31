package com.droiddeck.launcher.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droiddeck.launcher.R
import com.droiddeck.launcher.session.ShaderPrecompile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * The launched game's shaders compiling before it starts: a card over the session with the
 * count, a progress bar, the time left and Skip. Polls ShaderPrecompile's file twice a second
 * while the session runs; shows nothing when there is no file.
 */
@Composable
fun ShaderPrecompileCard(active: Boolean) {
    val context = LocalContext.current
    var progress by remember { mutableStateOf<ShaderPrecompile.Progress?>(null) }
    var skipping by remember { mutableStateOf(false) }
    LaunchedEffect(active) {
        while (active) {
            val now = withContext(Dispatchers.IO) { ShaderPrecompile.read(context) }
            if (now == null) skipping = false
            progress = now
            delay(500)
        }
        progress = null
    }
    val p = progress ?: return
    Box(Modifier.fillMaxSize().padding(top = 24.dp), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier
                .widthIn(min = 300.dp, max = 460.dp)
                .background(Color(0xE6101418), RoundedCornerShape(12.dp))
                .padding(horizontal = 18.dp, vertical = 14.dp),
        ) {
            Text(
                stringResource(R.string.shader_precompile_title, p.game.ifEmpty { stringResource(R.string.shader_precompile_the_game) }),
                color = Color.White, fontSize = 16.sp,
            )
            Spacer(Modifier.height(10.dp))
            if (p.total > 0) {
                LinearProgressIndicator(progress = { p.fraction }, modifier = Modifier.fillMaxWidth())
            } else {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text(detail(p), color = Color(0xFFB8C0C8), fontSize = 13.sp)
                if (!p.finished) {
                    TextButton(
                        enabled = !skipping,
                        onClick = {
                            skipping = true
                            ShaderPrecompile.skip(context)
                        },
                    ) { Text(stringResource(if (skipping) R.string.shader_precompile_skipping else R.string.shader_precompile_skip)) }
                }
            }
        }
    }
}

@Composable
private fun detail(p: ShaderPrecompile.Progress): String = when {
    p.state == "done" -> stringResource(R.string.shader_precompile_done)
    p.state == "failed" -> stringResource(R.string.shader_precompile_failed)
    p.state == "skipped" -> stringResource(R.string.shader_precompile_skipping)
    p.total <= 0 -> stringResource(R.string.shader_precompile_preparing)
    p.etaSeconds < 0 -> stringResource(R.string.shader_precompile_count, p.done, p.total)
    p.etaSeconds < 60 -> stringResource(R.string.shader_precompile_count_seconds, p.done, p.total, p.etaSeconds)
    else -> stringResource(R.string.shader_precompile_count_minutes, p.done, p.total, (p.etaSeconds + 59) / 60)
}
