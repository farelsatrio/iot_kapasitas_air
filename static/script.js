console.log("🚀 WebSocket dimulai...");

const ws = new WebSocket(`ws://${window.location.host}/ws`);

ws.onopen = function() {
    console.log("✅ WebSocket terhubung!");
};

ws.onclose = function(event) {
    console.log("❌ WebSocket terputus:", event);
    // Coba reconnect otomatis
    setTimeout(() => {
        console.log("🔄 Mencoba reconnect...");
        window.location.reload(); // Refresh halaman untuk reconnect
    }, 3000);
};

ws.onerror = function(error) {
    console.error("🚨 WebSocket error:", error);
};

ws.onmessage = function(event) {
    try {
        console.log("📥 Data diterima:", event.data); // <-- LOG INI HARUS MUNCUL!

        const data = JSON.parse(event.data);
        console.log("📦 Data parsed:", data);

        // Update Level Air
        let level = parseFloat(data.waterLevel);
        if (!isNaN(level)) {
            document.getElementById('waterLevel').textContent = `${level.toFixed(1)}%`;
        } else {
            document.getElementById('waterLevel').textContent = '--%';
        }

        // Update Status Pompa
        let pumpStatus = data.pumpStatus;
        if (typeof pumpStatus === 'string') {
            pumpStatus = pumpStatus.toLowerCase() === 'true';
        }
        document.getElementById('pumpStatus').textContent = pumpStatus ? 'NYALA' : 'MATI';

        // Update Mode
        let mode = data.mode || '';
        document.getElementById('mode').textContent = mode.toUpperCase();

        // Update Status
        let alert = data.alert || 'Normal';
        document.getElementById('alert').textContent = alert;
        document.getElementById('alert').className = alert === 'Normal' ? 
            'text-3xl font-bold text-green-600' : 
            'text-3xl font-bold text-yellow-600';

    } catch (e) {
        console.error("❌ Error memproses data WebSocket:", e);
        // Tetap tampilkan -- jika error
        document.getElementById('waterLevel').textContent = '--%';
        document.getElementById('pumpStatus').textContent = '--';
        document.getElementById('mode').textContent = '--';
        document.getElementById('alert').textContent = 'Error';
        document.getElementById('alert').className = 'text-3xl font-bold text-red-600';
    }
};

// Tombol kontrol
document.getElementById('autoBtn').onclick = () => {
    console.log("🔘 Kirim set_mode: automatic");
    ws.send(JSON.stringify({type: 'set_mode', value: 'automatic'}));
};

document.getElementById('manualBtn').onclick = () => {
    console.log("🔘 Kirim set_mode: manual");
    ws.send(JSON.stringify({type: 'set_mode', value: 'manual'}));
};

document.getElementById('pumpOnBtn').onclick = () => {
    console.log("🔘 Kirim set_pump: true");
    ws.send(JSON.stringify({type: 'set_pump', value: true}));
};

document.getElementById('pumpOffBtn').onclick = () => {
    console.log("🔘 Kirim set_pump: false");
    ws.send(JSON.stringify({type: 'set_pump', value: false}));
};
