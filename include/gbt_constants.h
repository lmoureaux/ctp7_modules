/*! \file
 *  \brief Header containing the constants for the GBT module.
 *  \author Laurent Pétré <lpetre@ulb.ac.be>
 */

#ifndef GBT_CONSTANTS_H
#define GBT_CONSTANTS_H

#include <stdint.h>
#include <array>

namespace gbt{
    /*! \brief The number of GBT's per OptoHybrid.
     */
    constexpr uint32_t gbtsPerOH = 3;

    /*! \brief The size of the GBT configuration address space. The corresponding addresses span from 0 to 365.
     */
    constexpr uint16_t configSize = 366;

    /*! \brief This type defines a GBT configuration blob.
     */
    typedef std::array<uint8_t, configSize> config_t;
}

#endif

