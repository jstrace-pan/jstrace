// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/antivirus_metrics_provider_win.h"

#include <iwscapi.h>
#include <stddef.h>
#include <wbemidl.h>
#include <windows.h>
#include <wscapi.h>

#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/file_version_info_win.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/version.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_comptr.h"
#include "base/win/scoped_variant.h"
#include "base/win/windows_version.h"
#include "chrome/common/channel_info.h"
#include "components/metrics/proto/system_profile.pb.h"
#include "components/variations/metrics_util.h"
#include "components/version_info/version_info.h"

namespace {

// This is an undocumented structure returned from querying the "productState"
// uint32 from the AntiVirusProduct in WMI.
// http://neophob.com/2010/03/wmi-query-windows-securitycenter2/ gives a good
// summary and testing was also done with a variety of AV products to determine
// these values as accurately as possible.
#pragma pack(push)
#pragma pack(1)
struct PRODUCT_STATE {
  uint8_t unknown_1 : 4;
  uint8_t definition_state : 4;  // 1 = Out of date, 0 = Up to date.
  uint8_t unknown_2 : 4;
  uint8_t security_state : 4;  //  0 = Inactive, 1 = Active, 2 = Snoozed.
  uint8_t security_provider;   // matches WSC_SECURITY_PROVIDER in wscapi.h.
  uint8_t unknown_3;
};
#pragma pack(pop)

static_assert(sizeof(PRODUCT_STATE) == 4, "Wrong packing!");

bool ShouldReportFullNames() {
  // The expectation is that this will be disabled for the majority of users,
  // but this allows a small group to be enabled on other channels if there are
  // a large percentage of hashes collected on these channels that are not
  // resolved to names previously collected on Canary channel.
  bool enabled = base::FeatureList::IsEnabled(
      AntiVirusMetricsProvider::kReportNamesFeature);

  if (chrome::GetChannel() == version_info::Channel::CANARY)
    return true;

  return enabled;
}

// Helper function for expanding all environment variables in |path|.
std::wstring ExpandEnvironmentVariables(const std::wstring& path) {
  static const DWORD kMaxBuffer = 32 * 1024;  // Max according to MSDN.
  std::wstring path_expanded;
  DWORD path_len = MAX_PATH;
  do {
    DWORD result = ExpandEnvironmentStrings(
        path.c_str(), base::WriteInto(&path_expanded, path_len), path_len);
    if (!result) {
      // Failed to expand variables. Return the original string.
      DPLOG(ERROR) << path;
      break;
    }
    if (result <= path_len)
      return path_expanded.substr(0, result - 1);
    path_len = result;
  } while (path_len < kMaxBuffer);

  return path;
}

// Helper function to take a |path| to a file, that might contain environment
// strings, and read the file version information in |product_version|. Returns
// true if it was possible to extract the file information correctly.
bool GetProductVersion(std::wstring* path, std::string* product_version) {
  base::FilePath full_path(ExpandEnvironmentVariables(*path));

#if !defined(_WIN64)
  if (!base::PathExists(full_path)) {
    // On 32-bit builds, path might contain C:\Program Files (x86) instead of
    // C:\Program Files.
    base::ReplaceFirstSubstringAfterOffset(path, 0, L"%ProgramFiles%",
                                           L"%ProgramW6432%");
    full_path = base::FilePath(ExpandEnvironmentVariables(*path));
  }
#endif  // !defined(_WIN64)
  std::unique_ptr<FileVersionInfo> version_info(
      FileVersionInfo::CreateFileVersionInfo(full_path));

  // It is not an error if the product version cannot be read, so continue in
  // this case.
  if (version_info.get()) {
    FileVersionInfoWin* version_info_win =
        static_cast<FileVersionInfoWin*>(version_info.get());
    std::string version_str =
        base::SysWideToUTF8(version_info_win->product_version());

    *product_version = std::move(version_str);
    return true;
  }

  return false;
}

}  // namespace

constexpr base::Feature AntiVirusMetricsProvider::kReportNamesFeature;

AntiVirusMetricsProvider::AntiVirusMetricsProvider(
    scoped_refptr<base::TaskRunner> task_runner)
    : task_runner_(task_runner), weak_ptr_factory_(this) {}

AntiVirusMetricsProvider::~AntiVirusMetricsProvider() {}

void AntiVirusMetricsProvider::ProvideSystemProfileMetrics(
    metrics::SystemProfileProto* system_profile_proto) {
  for (const auto& av_product : av_products_) {
    metrics::SystemProfileProto_AntiVirusProduct* product =
        system_profile_proto->add_antivirus_product();
    *product = av_product;
  }
}

void AntiVirusMetricsProvider::GetAntiVirusMetrics(
    const base::Closure& done_callback) {
  base::PostTaskAndReplyWithResult(
      task_runner_.get(), FROM_HERE,
      base::Bind(&AntiVirusMetricsProvider::GetAntiVirusProductsOnFileThread),
      base::Bind(&AntiVirusMetricsProvider::GotAntiVirusProducts,
                 weak_ptr_factory_.GetWeakPtr(), done_callback));
}

// static
std::vector<AntiVirusMetricsProvider::AvProduct>
AntiVirusMetricsProvider::GetAntiVirusProductsOnFileThread() {
  std::vector<AvProduct> av_products;

  ResultCode result = RESULT_GENERIC_FAILURE;

  base::win::OSInfo* os_info = base::win::OSInfo::GetInstance();

  // Windows Security Center APIs are not available on Server products.
  // See https://msdn.microsoft.com/en-us/library/bb432506.aspx.
  if (os_info->version_type() == base::win::SUITE_SERVER) {
    result = RESULT_WSC_NOT_AVAILABLE;
  } else {
    // The WSC interface is preferred here as it's fully documented, but only
    // available on Windows 8 and above, so instead use the undocumented WMI
    // interface on Windows 7 and below.
    if (os_info->version() >= base::win::VERSION_WIN8)
      result = FillAntiVirusProductsFromWSC(&av_products);
    else
      result = FillAntiVirusProductsFromWMI(&av_products);
  }

  UMA_HISTOGRAM_ENUMERATION("UMA.AntiVirusMetricsProvider.Result",
                            result,
                            RESULT_COUNT);

  return av_products;
}

void AntiVirusMetricsProvider::GotAntiVirusProducts(
    const base::Closure& done_callback,
    const std::vector<AvProduct>& av_products) {
  DCHECK(thread_checker_.CalledOnValidThread());
  av_products_ = av_products;
  done_callback.Run();
}

// static
AntiVirusMetricsProvider::ResultCode
AntiVirusMetricsProvider::FillAntiVirusProductsFromWSC(
    std::vector<AvProduct>* products) {
  std::vector<AvProduct> result_list;
  base::ThreadRestrictions::AssertIOAllowed();
  base::win::ScopedCOMInitializer com_initializer;

  if (!com_initializer.succeeded())
    return RESULT_FAILED_TO_INITIALIZE_COM;

  base::win::ScopedComPtr<IWSCProductList> product_list;
  HRESULT result =
      CoCreateInstance(__uuidof(WSCProductList), nullptr, CLSCTX_INPROC_SERVER,
                       __uuidof(IWSCProductList), product_list.ReceiveVoid());
  if (FAILED(result))
    return RESULT_FAILED_TO_CREATE_INSTANCE;

  result = product_list->Initialize(WSC_SECURITY_PROVIDER_ANTIVIRUS);
  if (FAILED(result))
    return RESULT_FAILED_TO_INITIALIZE_PRODUCT_LIST;

  LONG product_count;
  result = product_list->get_Count(&product_count);
  if (FAILED(result))
    return RESULT_FAILED_TO_GET_PRODUCT_COUNT;

  for (LONG i = 0; i < product_count; i++) {
    IWscProduct* product = nullptr;
    result = product_list->get_Item(i, &product);
    if (FAILED(result))
      return RESULT_FAILED_TO_GET_ITEM;

    static_assert(metrics::SystemProfileProto::AntiVirusState::
                          SystemProfileProto_AntiVirusState_STATE_ON ==
                      static_cast<metrics::SystemProfileProto::AntiVirusState>(
                          WSC_SECURITY_PRODUCT_STATE_ON),
                  "proto and API values must be the same");
    static_assert(metrics::SystemProfileProto::AntiVirusState::
                          SystemProfileProto_AntiVirusState_STATE_OFF ==
                      static_cast<metrics::SystemProfileProto::AntiVirusState>(
                          WSC_SECURITY_PRODUCT_STATE_OFF),
                  "proto and API values must be the same");
    static_assert(metrics::SystemProfileProto::AntiVirusState::
                          SystemProfileProto_AntiVirusState_STATE_SNOOZED ==
                      static_cast<metrics::SystemProfileProto::AntiVirusState>(
                          WSC_SECURITY_PRODUCT_STATE_SNOOZED),
                  "proto and API values must be the same");
    static_assert(metrics::SystemProfileProto::AntiVirusState::
                          SystemProfileProto_AntiVirusState_STATE_EXPIRED ==
                      static_cast<metrics::SystemProfileProto::AntiVirusState>(
                          WSC_SECURITY_PRODUCT_STATE_EXPIRED),
                  "proto and API values must be the same");

    AvProduct av_product;
    WSC_SECURITY_PRODUCT_STATE product_state;
    result = product->get_ProductState(&product_state);
    if (FAILED(result))
      return RESULT_FAILED_TO_GET_PRODUCT_STATE;

    if (!metrics::SystemProfileProto_AntiVirusState_IsValid(product_state))
      return RESULT_PRODUCT_STATE_INVALID;

    av_product.set_product_state(
        static_cast<metrics::SystemProfileProto::AntiVirusState>(
            product_state));

    base::win::ScopedBstr product_name;
    result = product->get_ProductName(product_name.Receive());
    if (FAILED(result))
      return RESULT_FAILED_TO_GET_PRODUCT_NAME;
    std::string name =
        base::SysWideToUTF8(std::wstring(product_name, product_name.Length()));
    product_name.Release();
    if (ShouldReportFullNames())
      av_product.set_product_name(name);
    av_product.set_product_name_hash(metrics::HashName(name));

    base::win::ScopedBstr remediation_path;
    result = product->get_RemediationPath(remediation_path.Receive());
    if (FAILED(result))
      return RESULT_FAILED_TO_GET_REMEDIATION_PATH;
    std::wstring path_str(remediation_path, remediation_path.Length());
    remediation_path.Release();

    std::string product_version;
    // Not a failure if the product version cannot be read from the file on
    // disk.
    if (GetProductVersion(&path_str, &product_version)) {
      if (ShouldReportFullNames())
        av_product.set_product_version(product_version);
      av_product.set_product_version_hash(metrics::HashName(product_version));
    }

    result_list.push_back(av_product);
  }

  *products = std::move(result_list);

  return RESULT_SUCCESS;
}

AntiVirusMetricsProvider::ResultCode
AntiVirusMetricsProvider::FillAntiVirusProductsFromWMI(
    std::vector<AvProduct>* products) {
  std::vector<AvProduct> result_list;
  base::ThreadRestrictions::AssertIOAllowed();
  base::win::ScopedCOMInitializer com_initializer;

  if (!com_initializer.succeeded())
    return RESULT_FAILED_TO_INITIALIZE_COM;

  base::win::ScopedComPtr<IWbemLocator> wmi_locator;
  HRESULT hr = wmi_locator.CreateInstance(CLSID_WbemLocator, nullptr,
                                          CLSCTX_INPROC_SERVER);
  if (FAILED(hr))
    return RESULT_FAILED_TO_CREATE_INSTANCE;

  base::win::ScopedComPtr<IWbemServices> wmi_services;
  hr = wmi_locator->ConnectServer(
      base::win::ScopedBstr(L"ROOT\\SecurityCenter2"), nullptr, nullptr,
      nullptr, 0, nullptr, nullptr, wmi_services.Receive());
  if (FAILED(hr))
    return RESULT_FAILED_TO_CONNECT_TO_WMI;

  hr = ::CoSetProxyBlanket(wmi_services.get(), RPC_C_AUTHN_WINNT,
                           RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  if (FAILED(hr))
    return RESULT_FAILED_TO_SET_SECURITY_BLANKET;

  // This interface is available on Windows Vista and above, and is officially
  // undocumented.
  base::win::ScopedBstr query_language(L"WQL");
  base::win::ScopedBstr query(L"SELECT * FROM AntiVirusProduct");
  base::win::ScopedComPtr<IEnumWbemClassObject> enumerator;

  hr = wmi_services->ExecQuery(
      query_language, query,
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      enumerator.Receive());
  if (FAILED(hr))
    return RESULT_FAILED_TO_EXEC_WMI_QUERY;

  // Iterate over the results of the WMI query. Each result will be an
  // AntiVirusProduct instance.
  while (true) {
    base::win::ScopedComPtr<IWbemClassObject> class_object;
    ULONG items_returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, class_object.Receive(),
                          &items_returned);
    if (FAILED(hr))
      return RESULT_FAILED_TO_ITERATE_RESULTS;

    if (hr == WBEM_S_FALSE || items_returned == 0)
      break;

    AvProduct av_product;
    av_product.set_product_state(
        metrics::SystemProfileProto::AntiVirusState::
            SystemProfileProto_AntiVirusState_STATE_ON);

    // See definition of PRODUCT_STATE structure above for how this is being
    // used.
    base::win::ScopedVariant product_state;
    hr = class_object->Get(L"productState", 0, product_state.Receive(), 0, 0);

    if (FAILED(hr) || product_state.type() != VT_I4)
      return RESULT_FAILED_TO_GET_PRODUCT_STATE;

    LONG state_val = V_I4(product_state.ptr());
    // Map the values from product_state to the proto values.
    switch (reinterpret_cast<PRODUCT_STATE*>(&state_val)->security_state) {
      case 0:
        av_product.set_product_state(
            metrics::SystemProfileProto::AntiVirusState::
                SystemProfileProto_AntiVirusState_STATE_OFF);
        break;
      case 1:
        av_product.set_product_state(
            metrics::SystemProfileProto::AntiVirusState::
                SystemProfileProto_AntiVirusState_STATE_ON);
        break;
      case 2:
        av_product.set_product_state(
            metrics::SystemProfileProto::AntiVirusState::
                SystemProfileProto_AntiVirusState_STATE_SNOOZED);
        break;
      default:
        // unknown state.
        return RESULT_PRODUCT_STATE_INVALID;
        break;
    }

    base::win::ScopedVariant display_name;
    hr = class_object->Get(L"displayName", 0, display_name.Receive(), 0, 0);

    if (FAILED(hr) || display_name.type() != VT_BSTR)
      return RESULT_FAILED_TO_GET_PRODUCT_NAME;

    // Owned by ScopedVariant.
    BSTR temp_bstr = V_BSTR(display_name.ptr());
    std::string name(base::SysWideToUTF8(
        std::wstring(temp_bstr, ::SysStringLen(temp_bstr))));

    if (ShouldReportFullNames())
      av_product.set_product_name(name);
    av_product.set_product_name_hash(metrics::HashName(name));

    base::win::ScopedVariant exe_path;
    hr = class_object->Get(L"pathToSignedProductExe", 0, exe_path.Receive(), 0,
                           0);

    if (FAILED(hr) || exe_path.type() != VT_BSTR)
      return RESULT_FAILED_TO_GET_REMEDIATION_PATH;

    temp_bstr = V_BSTR(exe_path.ptr());
    std::wstring path_str(temp_bstr, ::SysStringLen(temp_bstr));

    std::string product_version;
    // Not a failure if the product version cannot be read from the file on
    // disk.
    if (GetProductVersion(&path_str, &product_version)) {
      if (ShouldReportFullNames())
        av_product.set_product_version(product_version);
      av_product.set_product_version_hash(metrics::HashName(product_version));
    }

    result_list.push_back(av_product);
  }

  *products = std::move(result_list);

  return RESULT_SUCCESS;
}
