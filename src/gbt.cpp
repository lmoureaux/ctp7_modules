/*! \file
 *  \brief RPC module for GBT methods
 *  \author Laurent Pétré <lpetre@ulb.ac.be>
 */

#include "gbt.h"
#include "hw_constants.h"

#include "moduleapi.h"
#include "memhub.h"
#include "utils.h"

#include <thread>
#include <chrono>

void scanGBTPhases(const RPCMsg *request, RPCMsg *response){
    auto env = lmdb::env::create();
    env.set_mapsize(1UL * 1024UL * 1024UL * 40UL); /* 40 MiB */
    std::string gem_path = std::getenv("GEM_PATH");
    std::string lmdb_data_file = gem_path+"/address_table.mdb";
    env.open(lmdb_data_file.c_str(), 0, 0664);
    auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    auto dbi = lmdb::dbi::open(rtxn, nullptr);

    struct localArgs la = {.rtxn = rtxn, .dbi = dbi, .response = response};

    // Get the keys
    const uint32_t ohN = request->get_word("ohN");
    const uint32_t N = request->get_word("N");
    const uint8_t phaseMin = request->get_word("phaseMin");
    const uint8_t phaseMax = request->get_word("phaseMax");
    const uint8_t phaseStep = request->get_word("phaseStep");

    // Perform the scan
    std::vector<uint32_t> results;
    if (scanGBTPhasesLocal(&la, results, ohN, N, phaseMin, phaseMax, phaseStep))
        return;

    response->set_word_array("results", results);

    return;
} //Enc scanGBTPhase

bool scanGBTPhasesLocal(localArgs *la, std::vector<uint32_t> &results, const uint32_t ohN, const uint32_t N, const uint8_t phaseMin, const uint8_t phaseMax, const uint8_t phaseStep){
    LOGGER->log_message(LogManager::INFO, stdsprintf("Scanning the phases for OH #%u.", ohN));

    // ohN check
    const uint32_t ohMax = readReg(la, "GEM_AMC.GEM_SYSTEM.CONFIG.NUM_OF_OH");
    if (ohN >= ohMax)
        EMIT_RPC_ERROR(la->response, stdsprintf("The ohN parameter supplied (%u) exceeds the number of OH's supported by the CTP7 (%u).", ohN, ohMax), true)

    // phaseMin check
    if (phaseMin < gbt::PHASE_MIN)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phaseMin parameter supplied (%hhu) is smaller than the minimal phase (%hhu).", phaseMin, gbt::PHASE_MIN), true)
    if (phaseMin > gbt::PHASE_MAX)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phaseMin parameter supplied (%hhu) is bigger than the maximal phase (%hhu).", phaseMin, gbt::PHASE_MAX), true)

    // phaseMax check
    if (phaseMax < gbt::PHASE_MIN)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phaseMax parameter supplied (%hhu) is smaller than the minimal phase (%hhu).", phaseMax, gbt::PHASE_MIN), true)
    if (phaseMax > gbt::PHASE_MAX)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phaseMax parameter supplied (%hhu) is bigger than the maximal phase (%hhu).", phaseMax, gbt::PHASE_MAX), true)

    // Perform the scan
    for(uint8_t phase = phaseMin; phase <= phaseMax; phase += phaseStep){
        // Each measurement is composed as such :
        //  - [31:28] : OH number
        //  - [27:24] : Phase value
        //  - [23:0] : VFAT i status
        uint32_t result = ((ohN << 28) & 0xf0000000) |
                          ((phase << 24) & 0x0f000000) |
                          0x00ffffff;

        // Set the new phases
        for(uint32_t vfatN = 0; vfatN < oh::VFATS_PER_OH; vfatN++){
            if (writeGBTPhaseLocal(la, ohN, vfatN, phase))
                return true;
        }

        // Wait for the phases to be set
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (uint32_t repN = 0; repN < N; repN++){
            // Try to synchronize the VFAT's
            writeReg(la, "GEM_AMC.GEM_SYSTEM.CTRL.LINK_RESET", 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Check the VFAT status
            for(uint32_t vfatN = 0; vfatN < oh::VFATS_PER_OH; vfatN++){
                const bool linkGood = (readReg(la, stdsprintf("GEM_AMC.OH_LINKS.OH%hu.VFAT%hu.LINK_GOOD", ohN, vfatN)) == 1);
                const bool syncErrCnt = (readReg(la, stdsprintf("GEM_AMC.OH_LINKS.OH%hu.VFAT%hu.SYNC_ERR_CNT", ohN, vfatN)) == 0);
                const bool cfgRun = (readReg(la, stdsprintf("GEM_AMC.OH.OH%hu.GEB.VFAT%hu.CFG_RUN", ohN, vfatN)) != 0xdeaddead);

                // If no errors, the phase is good
                if (linkGood && syncErrCnt && cfgRun)
                    continue;

                result &= ~(0x1 << vfatN);
            }
        }

        results.push_back(result);
    }

    return false;
} //End scanGBTPhaseLocal

void writeGBTConfig(const RPCMsg *request, RPCMsg *response){
    auto env = lmdb::env::create();
    env.set_mapsize(1UL * 1024UL * 1024UL * 40UL); /* 40 MiB */
    std::string gem_path = std::getenv("GEM_PATH");
    std::string lmdb_data_file = gem_path+"/address_table.mdb";
    env.open(lmdb_data_file.c_str(), 0, 0664);
    auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    auto dbi = lmdb::dbi::open(rtxn, nullptr);

    struct localArgs la = {.rtxn = rtxn, .dbi = dbi, .response = response};

    // Get the keys
    const uint32_t ohN = request->get_word("ohN");
    const uint32_t gbtN = request->get_word("gbtN");

    // We must check the size of the config key
    const uint32_t configSize = request->get_binarydata_size("config");
    if (configSize != gbt::CONFIG_SIZE)
        EMIT_RPC_ERROR(response, stdsprintf("The provided configuration has not the correct size. It is %u registers long while this methods expects %hu 8-bits registers.", configSize, gbt::CONFIG_SIZE), );

    gbt::config_t config{};
    request->get_binarydata("config", config.data(), config.size());

    // Write the configuration
    writeGBTConfigLocal(&la, ohN, gbtN, config);

    return;
} //End writeGBTConfig(...)

bool writeGBTConfigLocal(localArgs *la, const uint32_t ohN, const uint32_t gbtN, const gbt::config_t &config){
    LOGGER->log_message(LogManager::INFO, stdsprintf("Writing the configuration of OH #%u - GBTX #%u.", ohN, gbtN));

    // ohN check
    const uint32_t ohMax = readReg(la, "GEM_AMC.GEM_SYSTEM.CONFIG.NUM_OF_OH");
    if (ohN >= ohMax)
        EMIT_RPC_ERROR(la->response, stdsprintf("The ohN parameter supplied (%u) exceeds the number of OH's supported by the CTP7 (%u).", ohN, ohMax), true)

    // gbtN check
    if (gbtN >= gbt::GBTS_PER_OH)
        EMIT_RPC_ERROR(la->response, stdsprintf("The gbtN parameter supplied (%u) exceeds the number of GBT's per OH (%u).", gbtN, gbt::GBTS_PER_OH), true)

    // Write all the registers
    for (size_t address = 0; address < gbt::CONFIG_SIZE; address++){
        if (writeGBTRegLocal(la, ohN, gbtN, static_cast<uint16_t>(address), config[address]))
            return true;
    }

    return false;
} //End writeGBTConfigLocal(...)

void writeGBTPhase(const RPCMsg *request, RPCMsg *response){
    auto env = lmdb::env::create();
    env.set_mapsize(1UL * 1024UL * 1024UL * 40UL); /* 40 MiB */
    std::string gem_path = std::getenv("GEM_PATH");
    std::string lmdb_data_file = gem_path+"/address_table.mdb";
    env.open(lmdb_data_file.c_str(), 0, 0664);
    auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    auto dbi = lmdb::dbi::open(rtxn, nullptr);

    struct localArgs la = {.rtxn = rtxn, .dbi = dbi, .response = response};

    // Get the keys
    const uint32_t ohN = request->get_word("ohN");
    const uint32_t vfatN = request->get_word("vfatN");
    const uint8_t phase = request->get_word("phase");

    // Write the phase
    writeGBTPhaseLocal(&la, ohN, vfatN, phase);

    return;
} //End writeGBTPhase

bool writeGBTPhaseLocal(localArgs *la, const uint32_t ohN, const uint32_t vfatN, const uint8_t phase){
    LOGGER->log_message(LogManager::INFO, stdsprintf("Writing the VFAT #%u phase of OH #%u.", vfatN, ohN));

    // ohN check
    const uint32_t ohMax = readReg(la, "GEM_AMC.GEM_SYSTEM.CONFIG.NUM_OF_OH");
    if (ohN >= ohMax)
        EMIT_RPC_ERROR(la->response, stdsprintf("The ohN parameter supplied (%u) exceeds the number of OH's supported by the CTP7 (%u).", ohN, ohMax), true)

    // vfatN check
    if (vfatN >= oh::VFATS_PER_OH)
        EMIT_RPC_ERROR(la->response, stdsprintf("The vfatN parameter supplied (%u) exceeds the number of VFAT's per OH (%u).", vfatN, oh::VFATS_PER_OH), true)

    // phase check
    if (phase < gbt::PHASE_MIN)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phase parameter supplied (%hhu) is smaller than the minimal phase (%hhu).", phase, gbt::PHASE_MIN), true)
    if (phase > gbt::PHASE_MAX)
        EMIT_RPC_ERROR(la->response, stdsprintf("The phase parameter supplied (%hhu) is bigger than the maximal phase (%hhu).", phase, gbt::PHASE_MAX), true)

    // Write the triplicated phase registers
    const uint32_t gbtN = gbt::elinkMappings::VFAT_TO_GBT[vfatN];

    for(unsigned char regN = 0; regN < 3; regN++){
        const uint16_t regAddress = gbt::elinkMappings::ELINK_TO_REGISTERS[gbt::elinkMappings::VFAT_TO_ELINK[vfatN]][regN];

        if (writeGBTRegLocal(la, ohN, gbtN, regAddress, phase))
            return true;
    }

    return false;
} //End writeGBTPhaseLocal

bool writeGBTRegLocal(localArgs *la, const uint32_t ohN, const uint32_t gbtN, const uint16_t address, const uint8_t value){
    // Check that the GBT exists
    if (gbtN >= gbt::GBTS_PER_OH)
        EMIT_RPC_ERROR(la->response, stdsprintf("The gbtN parameter supplied (%u) is larger than the number of GBT's per OH (%u).", gbtN, gbt::GBTS_PER_OH), true)

    // Check that the address is writable
    if (address >= gbt::CONFIG_SIZE)
        EMIT_RPC_ERROR(la->response, stdsprintf("GBT has %hu writable addresses while the provided address is %hu.", gbt::CONFIG_SIZE-1, address), true)

    // GBT registers are 8 bits long
    writeReg(la, "GEM_AMC.SLOW_CONTROL.IC.READ_WRITE_LENGTH", 1);

    // Select the link number
    const uint32_t linkN = ohN*gbt::GBTS_PER_OH + gbtN;
    writeReg(la, "GEM_AMC.SLOW_CONTROL.IC.GBTX_LINK_SELECT", linkN);

    // Write to the register
    writeReg(la, "GEM_AMC.SLOW_CONTROL.IC.ADDRESS", address);
    writeReg(la, "GEM_AMC.SLOW_CONTROL.IC.WRITE_DATA", value);
    writeReg(la, "GEM_AMC.SLOW_CONTROL.IC.EXECUTE_WRITE", 1);

    return false;
} //End writeGBTRegLocal(...)

extern "C" {
    const char *module_version_key = "gbt v1.0.1";
    int module_activity_color = 4;
    void module_init(ModuleManager *modmgr) {
        if (memhub_open(&memsvc) != 0) {
            LOGGER->log_message(LogManager::ERROR, stdsprintf("Unable to connect to memory service: %s", memsvc_get_last_error(memsvc)));
            LOGGER->log_message(LogManager::ERROR, "Unable to load module");
            return; // Do not register our functions, we depend on memsvc.
        }
        modmgr->register_method("gbt", "writeGBTConfig", writeGBTConfig);
        modmgr->register_method("gbt", "writeGBTPhase", writeGBTPhase);
        modmgr->register_method("gbt", "scanGBTPhases", scanGBTPhases);
    }
}

