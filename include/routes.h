#ifndef JOB_APP_ROUTES_H
#define JOB_APP_ROUTES_H

#include "httplib.h"
#include "scheduler.h"

struct AppState;

void registerRoutes(httplib::Server& server, AppState& state, Scheduler& scheduler);

#endif //JOB_APP_ROUTES_H
