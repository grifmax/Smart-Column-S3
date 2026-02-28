// ============================================================================

// Memory Statistics

// ============================================================================



export function updateMemoryStats(mem) {

    const memStatsDiv = document.getElementById('memory-stats');

    if (memStatsDiv.style.display === 'none') return;



    // Форматирование байтов в KB/MB

    const formatBytes = (bytes) => {

        if (bytes < 1024) return bytes + ' B';

        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';

        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';

    };



    // SRAM (Heap)

    const heapUsed = mem.heap_total - mem.heap_free;

    document.getElementById('mem-heap-used').textContent = formatBytes(heapUsed);

    document.getElementById('mem-heap-total').textContent = formatBytes(mem.heap_total);

    document.getElementById('mem-heap-pct').textContent = mem.heap_used_pct.toFixed(1) + '%';



    // PSRAM

    document.getElementById('mem-psram-free').textContent = formatBytes(mem.psram_free);

    document.getElementById('mem-psram-total').textContent = formatBytes(mem.psram_total);



    // Flash

    document.getElementById('mem-flash-pct').textContent = mem.flash_used_pct.toFixed(1) + '%';

}



export function toggleMemoryStats() {

    const checkbox = document.getElementById('show-memory-stats');

    const memStatsDiv = document.getElementById('memory-stats');



    if (checkbox.checked) {

        memStatsDiv.style.display = 'block';

        localStorage.setItem('showMemoryStats', 'true');

    } else {

        memStatsDiv.style.display = 'none';

        localStorage.setItem('showMemoryStats', 'false');

    }

}



export function loadMemoryStatsPreference() {

    const showMemoryStats = localStorage.getItem('showMemoryStats') === 'true';

    const checkbox = document.getElementById('show-memory-stats');

    const memStatsDiv = document.getElementById('memory-stats');



    if (checkbox) {

        checkbox.checked = showMemoryStats;

    }



    if (memStatsDiv) {

        memStatsDiv.style.display = showMemoryStats ? 'block' : 'none';

    }

}
