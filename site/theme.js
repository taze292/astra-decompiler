// Apply the saved preference before painting. A fresh visit starts in light mode.
try {
    const Theme = localStorage.getItem("astra-theme");
    if (Theme === "dark") { document.documentElement.dataset.theme = Theme; }
} catch { /* The site also works when browser storage is disabled. */ }
