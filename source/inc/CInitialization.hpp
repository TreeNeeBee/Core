/**
 * @file        CInitialization.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Runtime initialization and deinitialization functions
 * @date        2025-10-27
 * @details     Provides Initialize and Deinitialize functions for AUTOSAR Adaptive Runtime
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * </table>
 */
#ifndef LAP_CORE_INITIALIZATION_HPP
#define LAP_CORE_INITIALIZATION_HPP

#include "CTypedef.hpp"
#include "CResult.hpp"

namespace lap
{
namespace core
{
    // Initializes data structures and threads of the AUTOSAR Adaptive Runtime for Applications.
    // Prior to this call, no interaction with the ARA is possible. This call must be made inside of
    // main(), i.e., in a place where it is guaranteed that static memory initialization has completed.
    // Depending on the individual functional cluster specification, the calling application may have to
    // provide additional configuration data (e.g., set an Application ID for Logging) or make additional
    // initailization calls (e.g., start a FindService in lap::com) before other API calls to the respective
    // functional cluster can be made. Such calls must be made after the call to Initialize(). Calls to
    // ARA APIs made before static initialization has completed lead to undefinded behavior. Calls
    // made after static initialization has completed but before Initialize() was called will be rejected by
    // the functional cluster implementation with an error or, if no error to be reported is defined, lead
    // to undefined behavior.
    Result<void> Initialize () noexcept;

    // Destroy all data structures and threads of the AUTOSAR Adaptive Runtime for Applications.
    // After this call, no interaction with the ARA is possible. This call must be made inside of main(),
    // i.e., in a place where it is guaranteed that the static initialization has completed and destruciton
    // of statically initialized data has not yet started. Calls made to ARA APIs after a call to
    // lap::core::Deinitialize() but before destruction of statically initialized data will be rejected with an
    // error or, if no error is defined, lead to undefined behavior. Calls made to ARA APIs after the
    // destruction of statically initialized data will lead to undefined behavior.
    Result<void> Deinitialize () noexcept;

} // namespace core
} // namespace lap
#endif