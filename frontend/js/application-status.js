// Application statuses that mean the application is over.
// Shared by the tracker (grey-out, "active" filter) and the dashboard (hide closed applications).
export const CLOSED_APPLICATION_STATUSES = new Set(['declined', 'withdrawn', 'ghosted']);

export function isClosedApplication(job) {
  return CLOSED_APPLICATION_STATUSES.has(job.application_status);
}
