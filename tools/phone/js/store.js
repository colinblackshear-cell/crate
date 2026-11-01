// What the app keeps on the phone, in the browser's IndexedDB: the sound library and the kits.
// Sounds stay until you delete them; ask the browser to keep them (persist) so iOS doesn't
// clear them to free space.

const DB_NAME = "crate-phone";
let dbPromise = null;

function open()
{
    dbPromise ??= new Promise ((resolve, reject) =>
    {
        const req = indexedDB.open (DB_NAME, 1);
        req.onupgradeneeded = () =>
        {
            const db = req.result;
            db.createObjectStore ("sounds", { keyPath: "id" });   // { id, name, created, frames, channelCount }
            db.createObjectStore ("audio");                        // id -> [ArrayBuffer per channel]
            db.createObjectStore ("kits", { keyPath: "id" });
        };
        req.onsuccess = () => resolve (req.result);
        req.onerror = () => reject (req.error);
    });
    return dbPromise;
}

function run (storeNames, mode, work)
{
    return open().then ((db) => new Promise ((resolve, reject) =>
    {
        const tx = db.transaction (storeNames, mode);
        let result;
        Promise.resolve (work (tx)).then ((r) => { result = r; });
        tx.oncomplete = () => resolve (result);
        tx.onerror = () => reject (tx.error);
        tx.onabort = () => reject (tx.error);
    }));
}

const request = (req) => new Promise ((resolve, reject) => { req.onsuccess = () => resolve (req.result); req.onerror = () => reject (req.error); });

export async function persist()
{
    try { return await navigator.storage?.persist?.(); } catch { return false; }
}

export function listSounds()
{
    return run (["sounds"], "readonly", (tx) => request (tx.objectStore ("sounds").getAll()))
        .then ((all) => all.sort ((a, b) => b.created - a.created));
}

export async function saveSound (name, sound, extra = {})
{
    const id = crypto.randomUUID();
    const info = { id, name, created: Date.now(), frames: sound.channels[0].length, channelCount: sound.channels.length, ...extra };
    await run (["sounds", "audio"], "readwrite", (tx) =>
    {
        tx.objectStore ("sounds").put (info);
        tx.objectStore ("audio").put (sound.channels.map ((c) => c.slice().buffer), id);
    });
    return info;
}

export async function loadAudio (id)
{
    const buffers = await run (["audio"], "readonly", (tx) => request (tx.objectStore ("audio").get (id)));
    return buffers ? { channels: buffers.map ((b) => new Float32Array (b)) } : null;
}

export function renameSound (id, name)
{
    return run (["sounds"], "readwrite", async (tx) =>
    {
        const store = tx.objectStore ("sounds");
        const info = await request (store.get (id));
        if (info)
            store.put ({ ...info, name });
    });
}

export function deleteSound (id)
{
    return run (["sounds", "audio"], "readwrite", (tx) =>
    {
        tx.objectStore ("sounds").delete (id);
        tx.objectStore ("audio").delete (id);
    });
}

export function loadKit (id = "current")
{
    return run (["kits"], "readonly", (tx) => request (tx.objectStore ("kits").get (id)));
}

export function saveKit (kit)
{
    return run (["kits"], "readwrite", (tx) => { tx.objectStore ("kits").put ({ id: "current", ...kit }); });
}
