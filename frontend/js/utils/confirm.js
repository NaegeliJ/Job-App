export function showConfirm(message, onConfirm) {
  const overlay   = document.getElementById('confirm-overlay');
  const msgEl     = document.getElementById('confirm-message');
  const okBtn     = document.getElementById('confirm-ok-btn');
  const cancelBtn = document.getElementById('confirm-cancel-btn');
  if (!overlay || !msgEl || !okBtn || !cancelBtn) { if (confirm(message)) onConfirm(); return; }

  msgEl.textContent = message;
  overlay.classList.add('open');

  function cleanup() {
    overlay.classList.remove('open');
    okBtn.onclick = null;
    cancelBtn.onclick = null;
    overlay.onclick = null;
  }
  okBtn.onclick     = () => { cleanup(); onConfirm(); };
  cancelBtn.onclick = cleanup;
  overlay.onclick   = (e) => { if (e.target === overlay) cleanup(); };
}
