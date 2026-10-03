function addLog(msg, isError) {
    const box = document.getElementById("log-box");
    const time = new Date().toLocaleTimeString();
    const formatted = `[${time}] ${msg}`;
    console.log(formatted);
    if (box) {
        const color = isError ? "#ff5252" : "#a5d6a7";
        box.innerHTML = `<span style="color:${color};">${formatted}</span><br>` + box.innerHTML;
    }
}

function setLED(r, g, b) {
    const colorName = (r === 1 && g === 0 && b === 0) ? "Red" :
                      (r === 0 && g === 1 && b === 0) ? "Green" :
                      (r === 0 && g === 0 && b === 1) ? "Blue" :
                      (r === 0 && g === 0 && b === 0) ? "OFF" : `RGB(${r},${g},${b})`;

    addLog(`Button clicked: ${colorName} (r=${r}, g=${g}, b=${b})`);

    const statusElem = document.getElementById("status");
    if (statusElem) {
        statusElem.textContent = `Sending ${colorName}...`;
    }

    const url = `/setgpio?r=${r}&g=${g}&b=${b}`;
    addLog(`Fetching: ${url}`);

    fetch(url)
        .then(response => {
            addLog(`HTTP status: ${response.status} ${response.statusText}`);
            if (!response.ok) {
                throw new Error(`HTTP Error ${response.status}: ${response.statusText}`);
            }
            return response.text();
        })
        .then(text => {
            addLog(`ESP32 Response: "${text}"`);
            if (statusElem) {
                statusElem.textContent = `State: ${colorName}`;
                if (r === 1 && g === 0 && b === 0) statusElem.style.color = "#ef5350";
                else if (r === 0 && g === 1 && b === 0) statusElem.style.color = "#66bb6a";
                else if (r === 0 && g === 0 && b === 1) statusElem.style.color = "#42a5f5";
                else if (r === 0 && g === 0 && b === 0) statusElem.style.color = "#aaaaaa";
                else statusElem.style.color = "#ffffff";
            }
        })
        .catch(err => {
            addLog(`Request failed: ${err.message}`, true);
            if (statusElem) {
                statusElem.textContent = `Error: ${err.message}`;
                statusElem.style.color = "#ff5252";
            }
        });
}

// Log script load
addLog("app.js loaded successfully");
