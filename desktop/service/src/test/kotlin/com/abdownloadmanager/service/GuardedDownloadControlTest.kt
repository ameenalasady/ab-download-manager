package com.abdownloadmanager.service

import ir.amirab.downloader.DownloadManagerEvents
import ir.amirab.downloader.DownloadManagerMinimalControl
import ir.amirab.downloader.downloaditem.DownloadItemContext
import ir.amirab.downloader.downloaditem.EmptyContext
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlin.test.*

class GuardedDownloadControlTest {
    @Test fun scheduledTransitionsCannotEnterWhileMutationSuspendsAndFailureReleasesGate() = runBlocking {
        var mutating = false
        val calls = mutableListOf<String>()
        val engine = object : DownloadManagerMinimalControl {
            override val listOfJobsEvents = MutableSharedFlow<DownloadManagerEvents>()
            override fun canActivateJob(id: Long) = true
            override suspend fun startJob(id: Long, context: DownloadItemContext) { assertFalse(mutating); calls += "start:$id" }
            override suspend fun stopJob(id: Long, context: DownloadItemContext) { assertFalse(mutating); calls += "stop:$id" }
        }
        val control = GuardedDownloadControl(engine)
        val entered = CompletableDeferred<Unit>(); val release = CompletableDeferred<Unit>()
        val mutation = launch { control.mutate { mutating = true; entered.complete(Unit); release.await(); mutating = false } }
        entered.await()
        val start = launch { control.startJob(1, EmptyContext) }
        val stop = launch { control.stopJob(2, EmptyContext) }
        yield(); assertTrue(calls.isEmpty()) // A queue/scheduler ran during suspended disk I/O.
        release.complete(Unit); joinAll(mutation, start, stop)
        assertEquals(setOf("start:1", "stop:2"), calls.toSet())
        assertFailsWith<IllegalStateException> { control.mutate { error("fixture storage failure") } }
        withTimeout(1000) { control.startJob(3, EmptyContext) }
        assertEquals("start:3", calls.last())
        assertSame(engine.listOfJobsEvents, control.listOfJobsEvents)
    }
}
