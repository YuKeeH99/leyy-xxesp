(function () {
  var tracks = Array.prototype.slice.call(document.querySelectorAll('.track'));
  var verify = Array.prototype.slice.call(document.querySelectorAll('.verify-check'));
  var globalBar = document.getElementById('global-progress');

  function updateGlobal() {
    var total = tracks.length;
    var done = tracks.filter(function (c) { return c.checked; }).length;
    if (globalBar) globalBar.style.width = total ? (done / total * 100) + '%' : '0%';
    tracks.forEach(function (c) {
      var step = c.closest('.step');
      if (step) step.classList.toggle('is-done', c.checked);
    });
  }

  function updateVerify() {
    var total = verify.length;
    if (!total) return;
    var done = verify.filter(function (c) { return c.checked; }).length;
    var bar = document.getElementById('verify-bar');
    var label = document.getElementById('verify-label');
    if (bar) bar.style.width = (done / total * 100) + '%';
    if (label) label.textContent = done + ' / ' + total;
    verify.forEach(function (c) {
      var item = c.closest('.verify-item');
      if (item) item.classList.toggle('is-done', c.checked);
    });
  }

  tracks.forEach(function (c) { c.addEventListener('change', updateGlobal); });
  verify.forEach(function (c) { c.addEventListener('change', updateVerify); });
  updateGlobal();
  updateVerify();

  var reset = document.getElementById('reset-checks');
  if (reset) {
    reset.addEventListener('click', function () {
      tracks.concat(verify).forEach(function (c) { c.checked = false; });
      updateGlobal();
      updateVerify();
    });
  }

  var printBtn = document.getElementById('print-page');
  if (printBtn) printBtn.addEventListener('click', function () { window.print(); });

  var links = Array.prototype.slice.call(document.querySelectorAll('.toc a'));
  var sections = links
    .map(function (a) { return document.querySelector(a.getAttribute('href')); })
    .filter(Boolean);

  if ('IntersectionObserver' in window) {
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        links.forEach(function (a) {
          a.classList.toggle('active', a.getAttribute('href') === '#' + entry.target.id);
        });
      });
    }, { rootMargin: '-35% 0px -55% 0px' });
    sections.forEach(function (s) { observer.observe(s); });
  }

  var backtop = document.getElementById('backtop');
  if (backtop) {
    window.addEventListener('scroll', function () {
      backtop.classList.toggle('show', window.scrollY > 700);
    });
    backtop.addEventListener('click', function () {
      window.scrollTo({ top: 0, behavior: 'smooth' });
    });
  }
})();
