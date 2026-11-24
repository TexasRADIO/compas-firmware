export function stepEditorPage(steps, requested = 0) {
  if (!Number.isInteger(steps) || ![16, 24, 32, 48].includes(steps))
    throw Error('Unsupported editor step count.');
  const pages = steps > 32 ? Math.ceil(steps / 16) : 1;
  const page = Math.min(Math.max(requested, 0), pages - 1);
  const first = pages === 1 ? 0 : page * 16;
  return {page, pages, first, count: Math.min(steps - first, pages === 1 ? steps : 16)};
}
