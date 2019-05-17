#include "memory.h"

#include "moduleapi.h"
#include <libmemsvc.h>
#include "memhub.h"

memsvc_handle_t memsvc;

std::vector<std::uint32_t> Memory::Read::operator()(std::uint32_t address,
                                                    std::uint32_t count) const
{
    std::vector<std::uint32_t> result(count);
    if (0 == memhub_read(memsvc, address, count, &result.front())) {
        return result;
    } else {
        throw std::runtime_error(
            std::string("read memsvc error: ") + memsvc_get_last_error(memsvc));
    }
}

void Memory::Write::operator()(std::uint32_t address,
                               const std::vector<std::uint32_t> &data) const
{
    if (0 != memhub_write(memsvc, address, data.size(), &data.front())) {
        throw std::runtime_error(
            std::string("write memsvc error: ") + memsvc_get_last_error(memsvc));
    }
}

void mread(const RPCMsg *request, RPCMsg *response) {
	uint32_t count = request->get_word("count");
	uint32_t addr = request->get_word("address");
	uint32_t data[count];

	if (memhub_read(memsvc, addr, count, data) == 0) {
		response->set_word_array("data", data, count);
	}
	else {
		response->set_string("error", memsvc_get_last_error(memsvc));
		LOGGER->log_message(LogManager::INFO, stdsprintf("read memsvc error: %s", memsvc_get_last_error(memsvc)));
	}
}

void mwrite(const RPCMsg *request, RPCMsg *response) {
	uint32_t count = request->get_word_array_size("data");
	uint32_t data[count];
	request->get_word_array("data", data);
	uint32_t addr = request->get_word("address");

	if (memhub_write(memsvc, addr, count, data) != 0) {
		response->set_string("error", std::string("memsvc error: ")+memsvc_get_last_error(memsvc));
		LOGGER->log_message(LogManager::INFO, stdsprintf("write memsvc error: %s", memsvc_get_last_error(memsvc)));
	}
}

extern "C" {
	const char *module_version_key = "memory v1.0.1";
	int module_activity_color = 4;
	void module_init(ModuleManager *modmgr) {
		if (memhub_open(&memsvc) != 0) {
			LOGGER->log_message(LogManager::ERROR, stdsprintf("Unable to connect to memory service: %s", memsvc_get_last_error(memsvc)));
			LOGGER->log_message(LogManager::ERROR, "Unable to load module");
			return; // Do not register our functions, we depend on memsvc.
		}
		modmgr->register_method("memory", "read", mread);
		modmgr->register_method("memory", "write", mwrite);
		modmgr->register_method("memory",
                                        typeid(Memory::Read).name(),
                                        RPC::invoke<Memory::Read>);
		modmgr->register_method("memory",
                                        typeid(Memory::Write).name(),
                                        RPC::invoke<Memory::Write>);
	}
}
