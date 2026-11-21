// Home page: screenshot tabs
(function () {
  var img = document.getElementById("shotImg"), cap = document.getElementById("shotCap");
  if (img && cap) {
    var shots = [
      ["/shots/mac-vocoder.jpg", "Vocoder: thirteen pedals from mic to space, a mix strip, and the song strip ready to record a take over the drop."],
      ["/shots/mac-drone.jpg", "Drone box: four plucked strings, ready-made drones and eight knobs that follow your controller."],
      ["/shots/mac-ritual.jpg", "Drums: ritual hand-drum patterns with pitch per step, chance, conditions and swing."],
      ["/shots/mac-effects.jpg", "Effects: a pedalboard on any sound, including the line in."]
    ];
    var tabs = Array.prototype.slice.call(document.querySelectorAll(".tabs button"));
    var show = function (i) {
      img.src = shots[i][0]; img.alt = shots[i][1]; cap.textContent = shots[i][1];
      tabs.forEach(function (t) { t.setAttribute("aria-selected", String(+t.dataset.i === i)); });
    };
    tabs.forEach(function (t) { t.addEventListener("click", function () { show(+t.dataset.i); }); });
    show(1);
  }
  // Get page: copy the Terminal line
  var copy = document.getElementById("copy");
  if (copy) {
    copy.addEventListener("click", async function (e) {
      var code = document.getElementById("cmd");
      try { await navigator.clipboard.writeText(code.textContent); e.target.textContent = "Copied"; }
      catch (err) {
        var r = document.createRange(); r.selectNodeContents(code);
        var s = getSelection(); s.removeAllRanges(); s.addRange(r);
        e.target.textContent = "Press ⌘C";
      }
      setTimeout(function () { e.target.textContent = "Copy"; }, 2000);
    });
  }
})();
