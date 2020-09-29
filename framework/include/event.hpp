#pragma once
#include <gvk.hpp>

namespace gvk
{
	/** Base class for updater-events */
	class event
	{
	public:
		/**	Called frequently by the `gvk::updater`.
		 *	An event must evaluate whatever it has to evaluate and must return:
		 *	 -> true if the event has occured.
		 *	 -> false if the event has not occured.
		 *
		 *	@param	aData		Data storage where every event instance may gather
		 *						some data which might be used by updatees.
		 */
		virtual bool update(event_data& aData) = 0;
	};
}