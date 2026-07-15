/* ================================================================
   Smart-Column S3 — Landing Page JavaScript
   Pure vanilla JS, no dependencies
   ================================================================ */

'use strict';

/* ─── 1. Sticky Nav ─────────────────────────────────────────────── */
const nav = document.getElementById('lnd-nav');
if (nav) {
  window.addEventListener('scroll', () => {
    nav.classList.toggle('scrolled', window.scrollY > 60);
  }, { passive: true });
}

/* ─── 2. Animated Counters ──────────────────────────────────────── */
function animateCounter(el) {
  if (el.dataset.animated) return;
  el.dataset.animated = '1';
  const target   = parseFloat(el.dataset.target);
  const suffix   = el.dataset.suffix || '';
  const decimals = el.dataset.decimals ? parseInt(el.dataset.decimals, 10) : 0;
  const duration = 1400;
  const start    = performance.now();

  function step(now) {
    const elapsed  = now - start;
    const progress = Math.min(elapsed / duration, 1);
    const eased    = 1 - Math.pow(1 - progress, 3); // ease-out cubic
    el.textContent = (eased * target).toFixed(decimals) + suffix;
    if (progress < 1) {
      requestAnimationFrame(step);
    } else {
      el.textContent = target.toFixed(decimals) + suffix;
    }
  }
  requestAnimationFrame(step);
}

const statsBar = document.getElementById('stats-bar');
if (statsBar) {
  const counterIO = new IntersectionObserver((entries) => {
    entries.forEach(e => {
      if (e.isIntersecting) {
        document.querySelectorAll('.lnd-counter').forEach(animateCounter);
        counterIO.disconnect();
      }
    });
  }, { threshold: 0.25 });
  counterIO.observe(statsBar);
}

/* ─── 3. Fade-in-up on Scroll ───────────────────────────────────── */
const fadeEls = document.querySelectorAll('[data-animate="fade-up"]');
if (fadeEls.length) {
  const fadeIO = new IntersectionObserver((entries) => {
    entries.forEach(e => {
      if (e.isIntersecting) {
        // Stagger via delay already set inline; just add class
        e.target.classList.add('is-visible');
        fadeIO.unobserve(e.target);
      }
    });
  }, { threshold: 0.10, rootMargin: '0px 0px -30px 0px' });
  fadeEls.forEach(el => fadeIO.observe(el));
}

/* ─── 4. Comparison Table Row Highlight ─────────────────────────── */
document.querySelectorAll('.lnd-comp-table tbody tr:not(.group-row)').forEach(row => {
  row.addEventListener('mouseenter', () => row.style.background = 'rgba(0,123,255,0.05)');
  row.addEventListener('mouseleave', () => row.style.background = '');
});

/* ─── 5. Timeline Phase Tooltips (touch support) ────────────────── */
document.querySelectorAll('.lnd-phase').forEach(phase => {
  const tooltip = phase.querySelector('.lnd-phase-tooltip');
  if (!tooltip) return;
  phase.addEventListener('touchstart', (e) => {
    e.preventDefault();
    const isVisible = tooltip.classList.contains('touch-visible');
    // Close all others first
    document.querySelectorAll('.lnd-phase-tooltip.touch-visible').forEach(t => t.classList.remove('touch-visible'));
    if (!isVisible) tooltip.classList.add('touch-visible');
  }, { passive: false });
});

// Close tooltips when tapping elsewhere
document.addEventListener('touchstart', (e) => {
  if (!e.target.closest('.lnd-phase')) {
    document.querySelectorAll('.lnd-phase-tooltip.touch-visible').forEach(t => t.classList.remove('touch-visible'));
  }
}, { passive: true });

/* ─── 6. Smooth Scroll for Nav Links ────────────────────────────── */
document.querySelectorAll('a[href^="#"]').forEach(link => {
  link.addEventListener('click', (e) => {
    const id = link.getAttribute('href');
    if (id === '#') return;
    const target = document.querySelector(id);
    if (target) {
      e.preventDefault();
      target.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  });
});

/* ─── 7. SVG Column Hero Pulse on Load ──────────────────────────── */
// Add the 'heating' class to cube group so cubeGlow animation triggers
const heroSvg = document.getElementById('hero-column-svg');
if (heroSvg) {
  const cube = heroSvg.querySelector('#hero-cube');
  if (cube) cube.classList.add('heating');
}
