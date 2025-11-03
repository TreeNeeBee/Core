/**
 * @file        CCore.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Main header file for AUTOSAR Adaptive Platform Core module
 * @date        2025-10-29
 * @details     Aggregates all core functionality headers
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 compliant
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>add AUTOSAR standard containers
 * </table>
 */
#ifndef LAP_CORE_CORE_HPP
#define LAP_CORE_CORE_HPP

// Basic types and type definitions
#include "CTypedef.hpp"

// AUTOSAR standard containers and utilities (SWS_CORE_01xxx - 01xxx)
#include "CString.hpp"
#include "CSpan.hpp"
#include "COptional.hpp"
#include "CVariant.hpp"
#include "CUtility.hpp"

// Error handling (SWS_CORE_00xxx)
#include "CErrorDomain.hpp"
#include "CErrorCode.hpp"
#include "CException.hpp"
#include "CResult.hpp"
#include "CCoreErrorDomain.hpp"
#include "CFutureErrorDomain.hpp"

// Instance specifier and initialization (SWS_CORE_08xxx)
#include "CInstanceSpecifier.hpp"
#include "CInitialization.hpp"

// Concurrency and async operations (SWS_CORE_06xxx)
#include "CFuture.hpp"
#include "CPromise.hpp"

// Functional programming support (SWS_CORE_03xxx)
#include "CFunction.hpp"

// Algorithm utilities (SWS_CORE_18xxx)
#include "CAlgorithm.hpp"

// Byte order utilities (SWS_CORE_10xxx)
#include "CByteOrder.hpp"

// Platform utilities
#include "CAbort.hpp"
#include "CPath.hpp"
#include "CFile.hpp"

#endif