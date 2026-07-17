#ifndef HR_STANDALONE_SERVER_WINDOWS_SERVICE_H_
#define HR_STANDALONE_SERVER_WINDOWS_SERVICE_H_

#include "server/service-config.h"

namespace hr_standalone {

int RunWindowsService(const ServiceOverrides &overrides);

}  // namespace hr_standalone

#endif
