#include "nsyshid.h"
#include "Backend.h"
#include "BackendEmulated.h"
#if !defined(__ANDROID__) && !defined(VISIONOS)
#include "BackendLibusb.h"
#endif

namespace nsyshid::backend
{
	void AttachDefaultBackends()
	{
#if !defined(__ANDROID__) && !defined(VISIONOS)
		// add libusb backend
		{
			auto backendLibusb = std::make_shared<backend::libusb::BackendLibusb>();
			if (backendLibusb->IsInitialisedOk())
			{
				AttachBackend(backendLibusb);
			}
		}
#endif
	   // add emulated backend
		{
			auto backendEmulated = std::make_shared<backend::emulated::BackendEmulated>();
			if (backendEmulated->IsInitialisedOk())
			{
				AttachBackend(backendEmulated);
			}
		}
	}
} // namespace nsyshid::backend
