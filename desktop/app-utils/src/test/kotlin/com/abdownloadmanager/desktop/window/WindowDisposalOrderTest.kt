package com.abdownloadmanager.desktop.window

import androidx.compose.runtime.AbstractApplier
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Composition
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Recomposer
import kotlin.coroutines.EmptyCoroutineContext
import kotlin.test.Test
import kotlin.test.assertEquals

class WindowDisposalOrderTest {
    @Test
    fun laterHookRunsBeforeNestedWindowDisposal() {
        // CustomWindow registers its native-unmap hook after Window, in the outer
        // composition. Pin the ordering this relies on without opening a real window.
        val events = mutableListOf<String>()
        val recomposer = Recomposer(EmptyCoroutineContext)
        val composition = Composition(NoOpApplier(), recomposer)
        try {
            composition.setContent {
                SimulatedWindow(events)
                DisposableEffect(Unit) {
                    onDispose { events += "prepare" }
                }
            }
            assertEquals(emptyList(), events)
            composition.dispose()
            assertEquals(listOf("prepare", "dispose"), events)
        } finally {
            composition.dispose()
            recomposer.cancel()
        }
    }

    @Composable
    private fun SimulatedWindow(events: MutableList<String>) {
        DisposableEffect(Unit) {
            onDispose { events += "dispose" }
        }
    }

    private class NoOpApplier : AbstractApplier<Unit>(Unit) {
        override fun insertBottomUp(index: Int, instance: Unit) = Unit
        override fun insertTopDown(index: Int, instance: Unit) = Unit
        override fun move(from: Int, to: Int, count: Int) = Unit
        override fun onClear() = Unit
        override fun remove(index: Int, count: Int) = Unit
    }
}
