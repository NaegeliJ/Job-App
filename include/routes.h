#ifndef JOB_APP_ROUTES_H
#define JOB_APP_ROUTES_H

#include "httplib.h"

struct AppState;

void registerRoutes(httplib::Server& server, AppState& state);

#endif //JOB_APP_ROUTES_H
