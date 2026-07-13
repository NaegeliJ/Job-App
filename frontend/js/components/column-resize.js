const STORAGE_KEY = 'tracker_column_widths';
const MIN_WIDTH = 60;

function loadWidths() {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY)) || {};
  } catch {
    return {};
  }
}

export function initColumnResize(table) {
  if (!table) return;

  const headers = table.querySelectorAll('thead th');
  const widths = loadWidths();

  headers.forEach((header, index) => {
    // last column has no handle: it absorbs the leftover table width
    if (index === headers.length - 1) return;

    if (widths[index]) header.style.width = `${widths[index]}px`;

    const handle = document.createElement('div');
    handle.className = 'col-resize-handle';
    handle.title = 'Drag to resize';
    header.appendChild(handle);

    handle.addEventListener('mousedown', (e) => {
      e.preventDefault(); // no text selection while dragging
      const startX = e.clientX;
      const startWidth = header.offsetWidth;
      handle.classList.add('dragging');
      document.body.classList.add('col-resizing');

      function onMouseMove(event) {
        const width = Math.max(MIN_WIDTH, startWidth + event.clientX - startX);
        header.style.width = `${width}px`;
      }

      function onMouseUp() {
        document.removeEventListener('mousemove', onMouseMove);
        document.removeEventListener('mouseup', onMouseUp);
        handle.classList.remove('dragging');
        document.body.classList.remove('col-resizing');
        widths[index] = header.offsetWidth;
        localStorage.setItem(STORAGE_KEY, JSON.stringify(widths));
      }

      document.addEventListener('mousemove', onMouseMove);
      document.addEventListener('mouseup', onMouseUp);
    });
  });
}
