// Keeps the app working with no signal: every file is cached, and refreshed from the network
// whenever there is one.
const CACHE = "crate-phone-v1";
const FILES = ["./", "index.html", "style.css", "manifest.webmanifest", "icon.svg", "icon-180.png", "icon-512.png",
               "js/app.js", "js/audio.js", "js/capture-worklet.js", "js/dsp.js", "js/files.js", "js/mpc.js", "js/store.js"];

self.addEventListener ("install", (e) =>
{
    e.waitUntil (caches.open (CACHE).then ((c) => c.addAll (FILES)).then (() => self.skipWaiting()));
});

self.addEventListener ("activate", (e) =>
{
    e.waitUntil (caches.keys()
        .then ((keys) => Promise.all (keys.filter ((k) => k !== CACHE).map ((k) => caches.delete (k))))
        .then (() => self.clients.claim()));
});

self.addEventListener ("fetch", (e) =>
{
    if (e.request.method !== "GET")
        return;
    e.respondWith (fetch (e.request)
        .then ((response) =>
        {
            const copy = response.clone();
            caches.open (CACHE).then ((c) => c.put (e.request, copy));
            return response;
        })
        .catch (() => caches.match (e.request, { ignoreSearch: true })));
});
