#include <ospray/ospray.h>
#include <SDK/common/Managed.h>

namespace pvol
{
namespace osp_util
{
	void *
	GetIE(void *a)
	{
		ospray::ManagedObject *b = (ospray::ManagedObject *)a;
		return (void *)b->getIE();
	}
}
}
