/*
 * Library for USB automated driver installation - PKI part
 * Copyright (c) 2011 Pete Batard <pbatard@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <windows.h>
#include <setupapi.h>
#include <wincrypt.h>
#include <stdio.h>
#include <conio.h>
#include <stdint.h>
#include <config.h>
#include <string.h>

#include "installer.h"
#include "libwdi.h"
#include "logging.h"
#include "mssign32.h"
#include "msapi_utf8.h"

#define KEY_CONTAINER L"libwdi key container"
#ifndef CERT_STORE_PROV_SYSTEM_A
#define CERT_STORE_PROV_SYSTEM_A ((LPCSTR) 9)
#endif

extern char *windows_error_str(uint32_t retval);

/*
 * Crypt32.dll
 */
typedef HCERTSTORE (WINAPI *CertOpenStore_t)(
	LPCSTR lpszStoreProvider,
	DWORD dwMsgAndCertEncodingType,
	ULONG_PTR hCryptProv,
	DWORD dwFlags,
	const void *pvPara
);

typedef PCCERT_CONTEXT (WINAPI *CertCreateCertificateContext_t)(
	DWORD dwCertEncodingType,
	const BYTE *pbCertEncoded,
	DWORD cbCertEncoded
);

typedef PCCERT_CONTEXT (WINAPI *CertFindCertificateInStore_t)(
	HCERTSTORE hCertStore,
	DWORD dwCertEncodingType,
	DWORD dwFindFlags,
	DWORD dwFindType,
	const void *pvFindPara,
	PCCERT_CONTEXT pfPrevCertContext
);

typedef BOOL (WINAPI *CertAddCertificateContextToStore_t)(
	HCERTSTORE hCertStore,
	PCCERT_CONTEXT pCertContext,
	DWORD dwAddDisposition,
	PCCERT_CONTEXT *pStoreContext
);

typedef BOOL (WINAPI *CertDeleteCertificateFromStore_t)(
	PCCERT_CONTEXT pCertContext
);

typedef BOOL (WINAPI *CertFreeCertificateContext_t)(
	PCCERT_CONTEXT pCertContext
);

typedef BOOL (WINAPI *CertCloseStore_t)(
	HCERTSTORE hCertStore,
	DWORD dwFlags
);

typedef DWORD (WINAPI *CertGetNameStringA_t)(
	PCCERT_CONTEXT pCertContext,
	DWORD dwType,
	DWORD dwFlags,
	void *pvTypePara,
	LPCSTR pszNameString,
	DWORD cchNameString
);

typedef BOOL (WINAPI *CryptEncodeObject_t)(
	DWORD dwCertEncodingType,
	LPCSTR lpszStructType,
	const void *pvStructInfo,
	BYTE *pbEncoded,
	DWORD *pcbEncoded
);

typedef BOOL (WINAPI *CertStrToNameA_t)(
	DWORD dwCertEncodingType,
	LPCSTR pszX500,
	DWORD dwStrType,
	void *pvReserved,
	BYTE *pbEncoded,
	DWORD *pcbEncoded,
	LPCTSTR *ppszError
);

typedef BOOL (WINAPI *CryptAcquireCertificatePrivateKey_t)(
	PCCERT_CONTEXT pCert,
	DWORD dwFlags,
	void *pvReserved,
	ULONG_PTR *phCryptProvOrNCryptKey,
	DWORD *pdwKeySpec,
	BOOL *pfCallerFreeProvOrNCryptKey
);

// MiNGW32 doesn't know CERT_EXTENSIONS => redef
typedef struct _CERT_EXTENSIONS_ARRAY {
	DWORD cExtension;
	PCERT_EXTENSION rgExtension;
} CERT_EXTENSIONS_ARRAY, *PCERT_EXTENSIONS_ARRAY;

typedef PCCERT_CONTEXT (WINAPI *CertCreateSelfSignCertificate_t)(
	ULONG_PTR hCryptProvOrNCryptKey,
	PCERT_NAME_BLOB pSubjectIssuerBlob,
	DWORD dwFlags,
	PCRYPT_KEY_PROV_INFO pKeyProvInfo,
	PCRYPT_ALGORITHM_IDENTIFIER pSignatureAlgorithm,
	LPSYSTEMTIME pStartTime,
	LPSYSTEMTIME pEndTime,
	PCERT_EXTENSIONS_ARRAY pExtensions
);

/*
 * WinTrust.dll
 */

typedef struct CRYPTCATSTORE_ {
	DWORD      cbStruct;
	DWORD      dwPublicVersion;
	LPWSTR     pwszP7File;
	HCRYPTPROV hProv;
	DWORD      dwEncodingType;
	DWORD      fdwStoreFlags;
	HANDLE     hReserved;
	HANDLE     hAttrs;
	HCRYPTMSG  hCryptMsg;
	HANDLE     hSorted;
} CRYPTCATSTORE;

typedef HANDLE (WINAPI *CryptCATOpen_t)(
	LPWSTR pwszFileName,
	DWORD fdwOpenFlags,
	ULONG_PTR hProv,
	DWORD dwPublicVersion,
	DWORD dwEncodingType
);

typedef BOOL (WINAPI *CryptCATClose_t)(
	HANDLE hCatalog
);

typedef CRYPTCATSTORE* (WINAPI *CryptCATStoreFromHandle_t)(
	HANDLE hCatalog
);

typedef BOOL (WINAPI *CryptCATPersistStore_t)(
	HANDLE hCatalog
);

typedef BOOL (WINAPI *CryptCATAdminCalcHashFromFileHandle_t)(
	HANDLE hFile,
	DWORD *pcbHash,
	BYTE *pbHash,
	DWORD dwFlags
);


/*
 * Parts of the following functions are based on:
 * http://blogs.msdn.com/b/alejacma/archive/2009/03/16/how-to-create-a-self-signed-certificate-with-cryptoapi-c.aspx
 * http://blogs.msdn.com/b/alejacma/archive/2008/12/11/how-to-sign-exe-files-with-an-authenticode-certificate-part-2.aspx
 * http://www.jensign.com/hash/index.html
 */

/*
 * Add a certificate, identified by its pCertContext, to the system store 'szStoreName'
 */
BOOL AddCertToStore(PCCERT_CONTEXT pCertContext, LPCSTR szStoreName)
{
	PF_DECL(CertOpenStore);
	PF_DECL(CertAddCertificateContextToStore);
	PF_DECL(CertCloseStore);
	HCERTSTORE hSystemStore = NULL;

	PF_INIT(CertOpenStore, crypt32);
	PF_INIT(CertAddCertificateContextToStore, crypt32);
	PF_INIT(CertCloseStore, crypt32);
	if ((pfCertOpenStore == NULL) || (pfCertAddCertificateContextToStore == NULL) || (pfCertCloseStore == NULL)) {
		wdi_warn("unable to access crypt32 DLL");
		return FALSE;
	}

	hSystemStore = pfCertOpenStore(CERT_STORE_PROV_SYSTEM_A, X509_ASN_ENCODING,
		0, CERT_SYSTEM_STORE_LOCAL_MACHINE, szStoreName);
	if (hSystemStore == NULL) {
		wdi_warn("failed to open system store '%s': %s", szStoreName, windows_error_str(0));
		return FALSE;
	} 

	if (!pfCertAddCertificateContextToStore(hSystemStore, pCertContext, CERT_STORE_ADD_REPLACE_EXISTING, NULL)) {
		wdi_warn("failed to add certificate to system store '%s': %s", szStoreName, windows_error_str(0));
		pfCertCloseStore(hSystemStore, 0);
		return FALSE;
	} 

	pfCertCloseStore(hSystemStore, 0);
	return TRUE;
}

/*
 * Remove a certificate, identified by its subject, to the system store 'szStoreName'
 */
void RemoveCertFromStore(LPCSTR szCertSubject, LPCSTR szStoreName)
{
	PF_DECL(CertOpenStore);
	PF_DECL(CertFindCertificateInStore);
	PF_DECL(CertDeleteCertificateFromStore);
	PF_DECL(CertCloseStore);
	PF_DECL(CertStrToNameA);
	HCERTSTORE hSystemStore = NULL;
	PCCERT_CONTEXT pCertContext;
	CERT_NAME_BLOB certNameBlob = {0, NULL};
	
	PF_INIT(CertOpenStore, crypt32);
	PF_INIT(CertFindCertificateInStore, crypt32);
	PF_INIT(CertDeleteCertificateFromStore, crypt32);
	PF_INIT(CertCloseStore, crypt32);
	PF_INIT(CertStrToNameA, crypt32);

	if ( (pfCertOpenStore == NULL) || (pfCertDeleteCertificateFromStore == NULL) // || (pfCertDuplicateCertificateContext == NULL)
	  || (pfCertFindCertificateInStore == NULL) || (pfCertCloseStore == NULL) || (pfCertStrToNameA == NULL) ) {
		wdi_warn("unable to access crypt32 DLL");
		goto out;
	}

	hSystemStore = pfCertOpenStore(CERT_STORE_PROV_SYSTEM_A, X509_ASN_ENCODING,
		0, CERT_SYSTEM_STORE_LOCAL_MACHINE, szStoreName);
	if (hSystemStore == NULL) {
		wdi_warn("failed to open system store '%s': %s", szStoreName, windows_error_str(0));
		goto out;
	} 

	// Encode Cert Name
	if ( (!pfCertStrToNameA(X509_ASN_ENCODING, szCertSubject, CERT_X500_NAME_STR, NULL, NULL, &certNameBlob.cbData, NULL))
	  || ((certNameBlob.pbData = (BYTE*)malloc(certNameBlob.cbData)) == NULL)
	  || (!pfCertStrToNameA(X509_ASN_ENCODING, szCertSubject, CERT_X500_NAME_STR, NULL, certNameBlob.pbData, &certNameBlob.cbData, NULL)) ) {
		wdi_warn("failed to encode'%s': %s", szCertSubject, windows_error_str(0));
		goto out;
	}

	pCertContext = NULL;
	while ((pCertContext = pfCertFindCertificateInStore(hSystemStore, X509_ASN_ENCODING, 0,
		CERT_FIND_SUBJECT_NAME, (const void*)&certNameBlob, NULL)) != NULL) {
		pfCertDeleteCertificateFromStore(pCertContext);
		wdi_info("deleted obsolete '%s' from '%s' store", szCertSubject, szStoreName);
	}

out:
	if (certNameBlob.pbData != NULL) free (certNameBlob.pbData);
	if (hSystemStore != NULL) pfCertCloseStore(hSystemStore, 0);
}

/*
 * Add certificate data to the TrustedPublisher system store
 * Unless bDisableWarning is set, warn the user before install
 */
int AddCertToTrustedPublisher(BYTE* pbCertData, DWORD dwCertSize, BOOL bDisableWarning, HWND hWnd)
{
	PF_DECL(CertOpenStore);
	PF_DECL(CertCreateCertificateContext);
	PF_DECL(CertFindCertificateInStore);
	PF_DECL(CertAddCertificateContextToStore);
	PF_DECL(CertFreeCertificateContext);
	PF_DECL(CertGetNameStringA);
	PF_DECL(CertCloseStore);
	int r = WDI_ERROR_OTHER, user_input;
	HCERTSTORE hSystemStore;
	PCCERT_CONTEXT pCertContext, pStoreCertContext = NULL;
	char org[STR_BUFFER_SIZE], org_unit[STR_BUFFER_SIZE];
	char msg_string[1024];

	PF_INIT(CertOpenStore, crypt32);
	PF_INIT(CertCreateCertificateContext, crypt32);
	PF_INIT(CertFindCertificateInStore, crypt32);
	PF_INIT(CertAddCertificateContextToStore, crypt32);
	PF_INIT(CertFreeCertificateContext, crypt32);
	PF_INIT(CertGetNameStringA, crypt32);
	PF_INIT(CertCloseStore, crypt32);
	if ( (pfCertOpenStore == NULL) || (pfCertCreateCertificateContext == NULL)	|| (pfCertFindCertificateInStore == NULL)
	  || (pfCertAddCertificateContextToStore == NULL) || (pfCertFreeCertificateContext == NULL)
	  || (pfCertCloseStore == NULL) || (pfCertGetNameStringA == NULL) ) {
		wdi_warn("unable to access crypt32 DLL");
		return WDI_ERROR_RESOURCE;
	}

	hSystemStore = pfCertOpenStore((LPCSTR)CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING,
		0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"TrustedPublisher");

	if (hSystemStore == NULL) {
		wdi_err("unable to open system store: %s", windows_error_str(0));
		return WDI_ERROR_ACCESS;
	}

	/* Check whether certificate already exists
	 * We have to do this manually, so that we can produce a warning to the user
	 * before any certificate is added to the store (first time or update)
	 */
	pCertContext = pfCertCreateCertificateContext(X509_ASN_ENCODING, pbCertData, dwCertSize);

	if (pCertContext == NULL) {
		wdi_err("could not create context for certificate: %s", windows_error_str(0));
		pfCertCloseStore(hSystemStore, 0);
		return WDI_ERROR_ACCESS;
	}

	pStoreCertContext = pfCertFindCertificateInStore(hSystemStore, X509_ASN_ENCODING, 0,
		CERT_FIND_EXISTING, (const void*)pCertContext, NULL);
	if (pStoreCertContext == NULL) {
		user_input = IDOK;
		if (!bDisableWarning) {
			org[0] = 0; org_unit[0] = 0;
			pfCertGetNameStringA(pCertContext, CERT_NAME_ATTR_TYPE, 0, szOID_ORGANIZATION_NAME, org, sizeof(org));
			pfCertGetNameStringA(pCertContext, CERT_NAME_ATTR_TYPE, 0, szOID_ORGANIZATIONAL_UNIT_NAME, org_unit, sizeof(org_unit));
			safe_sprintf(msg_string, sizeof(msg_string), "Warning: this software is about to install the following organization\n"
				"as a Trusted Publisher on your system:\n\n '%s%s%s%s'\n\n"
				"This will allow this Publisher to run software with elevated privileges,\n"
				"as well as install driver packages, without further security notices.\n\n"
				"If this is not what you want, you can cancel this operation now.", org,
				(org_unit[0] != 0)?" (":"", org_unit, (org_unit[0] != 0)?")":"");
				user_input = MessageBoxA(hWnd, msg_string,
					"Warning: Trusted Certificate installation", MB_OKCANCEL | MB_ICONWARNING);
		}
		if (user_input != IDOK) {
			wdi_info("operation cancelled by the user");
			r = WDI_ERROR_USER_CANCEL;
		} else {
			if (!pfCertAddCertificateContextToStore(hSystemStore, pCertContext, CERT_STORE_ADD_NEWER, NULL)) {
				wdi_err("could not add certificate: %s", windows_error_str(0));
				r = WDI_ERROR_ACCESS;
			} else {
				r = WDI_SUCCESS;
			}
		}
	} else {
		r = WDI_ERROR_EXISTS;
	}

	if (pCertContext != NULL) pfCertFreeCertificateContext(pCertContext);
	if (pStoreCertContext != NULL) pfCertFreeCertificateContext(pStoreCertContext);

	if (!pfCertCloseStore(hSystemStore, 0)) {
		wdi_warn("unable to close the system store: %s", windows_error_str(0));
	}
	return r;
}

/*
 * Create a self signed certificate for code signing
 */
// TODO: set "libwdi" as friendly name
PCCERT_CONTEXT CreateSelfSignedCert(LPCSTR szCertSubject)
{
	PF_DECL(CryptEncodeObject);
	PF_DECL(CertStrToNameA);
	PF_DECL(CertCreateSelfSignCertificate);
	PF_DECL(CertFreeCertificateContext);

	HCRYPTPROV hCSP = 0;
	HCRYPTKEY hKey = 0;
	PCCERT_CONTEXT pCertContext = NULL;
	CERT_NAME_BLOB SubjectIssuerBlob = {0, NULL};
	CRYPT_KEY_PROV_INFO KeyProvInfo;
	CRYPT_ALGORITHM_IDENTIFIER SignatureAlgorithm;
	LPWSTR wszKeyContainer = KEY_CONTAINER;
	DWORD dwSize;
	CERT_EXTENSION certExtension;
	CERT_EXTENSIONS_ARRAY certExtensionsArray;
	LPSTR szCertPolicyElementId = "1.3.6.1.5.5.7.3.3"; // szOID_PKIX_KP_CODE_SIGNING;
	CERT_ENHKEY_USAGE certEnhKeyUsage = { 1, &szCertPolicyElementId };
	BYTE* pbEnhKeyUsage = NULL;
	BOOL success = FALSE;

	PF_INIT(CryptEncodeObject, crypt32);
	PF_INIT(CertStrToNameA, crypt32);
	PF_INIT(CertCreateSelfSignCertificate, crypt32);
	PF_INIT(CertFreeCertificateContext, crypt32);
	if ( (pfCryptEncodeObject == NULL) || (pfCertFreeCertificateContext == NULL)
	  || (pfCertStrToNameA == NULL) ||  (pfCertCreateSelfSignCertificate == NULL) ) {
		wdi_warn("unable to access crypt32 DLL: %s", windows_error_str(0));
		goto out;
	}

	// Set Enhanced Key Usage extension to Code Signing only
	if ( (!pfCryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, (LPVOID)&certEnhKeyUsage, NULL, &dwSize))
	  || ((pbEnhKeyUsage = (BYTE*)malloc(dwSize)) == NULL)
	  || (!pfCryptEncodeObject(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, (LPVOID)&certEnhKeyUsage, pbEnhKeyUsage, &dwSize)) ) {
		wdi_warn("could not setup EKU for Code Signing: %s", windows_error_str(0));
		goto out;
	}
	certExtension.pszObjId = szOID_ENHANCED_KEY_USAGE;
	certExtension.fCritical = TRUE;		// only allow code signing
	certExtension.Value.cbData = dwSize;
	certExtension.Value.pbData = pbEnhKeyUsage;
	certExtensionsArray.cExtension = 1;
	certExtensionsArray.rgExtension = &certExtension;
	wdi_dbg("set code signing EKU");

	if (CryptAcquireContextW(&hCSP, wszKeyContainer, NULL, PROV_RSA_FULL, CRYPT_MACHINE_KEYSET|CRYPT_SILENT)) {
		wdi_dbg("acquired existing key container");
	} else if ( (GetLastError() == NTE_BAD_KEYSET) 
			 && (CryptAcquireContextW(&hCSP, wszKeyContainer, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET|CRYPT_MACHINE_KEYSET|CRYPT_SILENT)) ) {
		wdi_dbg("created new key container");
	} else {
		wdi_warn("could not obtain a key container: %s", windows_error_str(0));
		goto out;
	}

	// Generate key pair (0x0400XXXX = RSA 1024 bit)
	if (!CryptGenKey(hCSP, AT_SIGNATURE, 0x04000000 | CRYPT_EXPORTABLE, &hKey)) {
		wdi_dbg("could not generate keypair: %s", windows_error_str(0));
		goto out;
	}
	wdi_dbg("generated new keypair");

	if (hKey) CryptDestroyKey(hKey);
	if (hCSP) CryptReleaseContext(hCSP, 0);
	
	if ( (!pfCertStrToNameA(X509_ASN_ENCODING, szCertSubject, CERT_X500_NAME_STR, NULL, NULL, &SubjectIssuerBlob.cbData, NULL))
	  || ((SubjectIssuerBlob.pbData = (BYTE*)malloc(SubjectIssuerBlob.cbData)) == NULL)
	  || (!pfCertStrToNameA(X509_ASN_ENCODING, szCertSubject, CERT_X500_NAME_STR, NULL, SubjectIssuerBlob.pbData, &SubjectIssuerBlob.cbData, NULL)) ) {
		wdi_warn("could not encode subject name for self signed cert: %s", windows_error_str(0));
		goto out;
	}
	wdi_dbg("encoded subject name: '%s'", szCertSubject);

	// Prepare key provider structure for self-signed certificate
	memset(&KeyProvInfo, 0, sizeof(KeyProvInfo));
	KeyProvInfo.pwszContainerName = wszKeyContainer;
	KeyProvInfo.pwszProvName = NULL;
	KeyProvInfo.dwProvType = PROV_RSA_FULL;
	KeyProvInfo.dwFlags = CRYPT_MACHINE_KEYSET;
	KeyProvInfo.cProvParam = 0;
	KeyProvInfo.rgProvParam = NULL;
	KeyProvInfo.dwKeySpec = AT_SIGNATURE;

	// Prepare algorithm structure for self-signed certificate
	memset(&SignatureAlgorithm, 0, sizeof(SignatureAlgorithm));
	SignatureAlgorithm.pszObjId = szOID_RSA_SHA1RSA;

	// Create self-signed certificate
	pCertContext = pfCertCreateSelfSignCertificate((ULONG_PTR)NULL,
		&SubjectIssuerBlob, 0, &KeyProvInfo, &SignatureAlgorithm, NULL, NULL, &certExtensionsArray);
	if (pCertContext == NULL) {
		wdi_warn("could not create self signed certificate: %s", windows_error_str(0));
		goto out;
	}
	success = TRUE;

out:
	if (pbEnhKeyUsage != NULL) free(pbEnhKeyUsage);
	if (SubjectIssuerBlob.pbData != NULL) free(SubjectIssuerBlob.pbData);
	if (hCSP) CryptReleaseContext(hCSP, 0);
	if ((!success) && (pCertContext != NULL)) {
		pfCertFreeCertificateContext(pCertContext);
		pCertContext = NULL;
	}
	return pCertContext;
}

/*
 * Delete the private key associated with a specific cert
 */
BOOL DeletePrivateKey(PCCERT_CONTEXT pCertContext)
{
	PF_DECL(CryptAcquireCertificatePrivateKey);

	LPWSTR wszKeyContainer = KEY_CONTAINER;
	HCRYPTPROV hCSP = 0;
	DWORD dwKeySpec;
	BOOL bFreeCSP, retval = FALSE;

	PF_INIT(CryptAcquireCertificatePrivateKey, crypt32);
	if (pfCryptAcquireCertificatePrivateKey == NULL) {
		wdi_warn("unable to access crypt32 DLL: %s", windows_error_str(0));
		goto out;
	}

	if (!pfCryptAcquireCertificatePrivateKey(pCertContext, 0, NULL, &hCSP, &dwKeySpec, &bFreeCSP)) {
		wdi_warn("error getting CSP: %s", windows_error_str(0));
		goto out;
	}

	if (CryptAcquireContextW(&hCSP, wszKeyContainer, NULL, PROV_RSA_FULL, CRYPT_MACHINE_KEYSET|CRYPT_DELETEKEYSET)) {
		retval = TRUE;
	} else {
		wdi_warn("failed to delete private key: %s", windows_error_str(0));
	}

out:
	if ((bFreeCSP) && (hCSP)) {
		CryptReleaseContext(hCSP, 0);
	}
	return retval;
}

/*
 * Digitally sign a file and make it system-trusted by:
 * - creating a self signed certificate for code signing
 * - adding this certificate to both the Root and TrustedPublisher system stores
 * - signing the file provided
 * - deleting the self signed certificate private key so that it cannot be reused
 */
int SelfSignFile(LPCSTR szFileName, LPCSTR szCertSubject)
{
	PF_DECL(SignerSignEx);
	PF_DECL(SignerFreeSignerContext);
	PF_DECL(CertFreeCertificateContext);
	PF_DECL(CertCloseStore);

	int r = WDI_ERROR_RESOURCE;
	LPWSTR wszFileName = NULL;
	HRESULT hResult = S_OK;
	HCERTSTORE hCertStore = NULL; 
	PCCERT_CONTEXT pCertContext = NULL;
	DWORD dwIndex;
	SIGNER_FILE_INFO signerFileInfo;
	SIGNER_SUBJECT_INFO signerSubjectInfo;
	SIGNER_CERT_STORE_INFO signerCertStoreInfo;
	SIGNER_CERT signerCert;
	SIGNER_SIGNATURE_INFO signerSignatureInfo;
	PSIGNER_CONTEXT pSignerContext = NULL;
	CRYPT_ATTRIBUTES_ARRAY cryptAttributesArray;
	CRYPT_ATTRIBUTE cryptAttribute[2];
	CRYPT_INTEGER_BLOB oidSpOpusInfoBlob, oidStatementTypeBlob;
	BYTE pbOidSpOpusInfo[] = SP_OPUS_INFO_DATA;
	BYTE pbOidStatementType[] = STATEMENT_TYPE_DATA;

	PF_INIT(SignerSignEx, mssign32);
	PF_INIT(SignerFreeSignerContext, mssign32);
	PF_INIT(CertFreeCertificateContext, crypt32);
	PF_INIT(CertCloseStore, crypt32);

	if ( (pfSignerSignEx == NULL) || (pfSignerFreeSignerContext == NULL)
	  || (pfCertFreeCertificateContext == NULL) || (pfCertCloseStore == NULL) ) {
		wdi_warn("unable to access mssign32 or crypt32 DLL: %s", windows_error_str(0));
		goto out;
	}

	// Delete any previous certificate with the same subject
	RemoveCertFromStore(szCertSubject, "Root");
	RemoveCertFromStore(szCertSubject, "TrustedPublisher");

	pCertContext = CreateSelfSignedCert(szCertSubject);
	if (pCertContext == NULL) {
		goto out;
	}
	wdi_dbg("successfully created certificate '%s'", szCertSubject);
	if ( (!AddCertToStore(pCertContext, "Root"))
	  || (!AddCertToStore(pCertContext, "TrustedPublisher")) ) {
		goto out;
	}
	wdi_info("added cert '%s' to 'Root' and 'TrustedPublisher' stores", szCertSubject);

	// Setup SIGNER_FILE_INFO struct
	signerFileInfo.cbSize = sizeof(SIGNER_FILE_INFO);
	wszFileName = utf8_to_wchar(szFileName);
	if (wszFileName == NULL) {
		wdi_warn("unable to convert '%s' to UTF16");
		goto out;
	}
	signerFileInfo.pwszFileName = wszFileName;
	signerFileInfo.hFile = NULL;

	// Prepare SIGNER_SUBJECT_INFO struct
	signerSubjectInfo.cbSize = sizeof(SIGNER_SUBJECT_INFO);
	dwIndex = 0;
	signerSubjectInfo.pdwIndex = &dwIndex;
	signerSubjectInfo.dwSubjectChoice = SIGNER_SUBJECT_FILE;
	signerSubjectInfo.pSignerFileInfo = &signerFileInfo;

	// Prepare SIGNER_CERT_STORE_INFO struct
	signerCertStoreInfo.cbSize = sizeof(SIGNER_CERT_STORE_INFO);
	signerCertStoreInfo.pSigningCert = pCertContext;
	signerCertStoreInfo.dwCertPolicy = SIGNER_CERT_POLICY_CHAIN;
	signerCertStoreInfo.hCertStore = NULL;

	// Prepare SIGNER_CERT struct
	signerCert.cbSize = sizeof(SIGNER_CERT);
	signerCert.dwCertChoice = SIGNER_CERT_STORE;
	signerCert.pCertStoreInfo = &signerCertStoreInfo;
	signerCert.hwnd = NULL;

	// Prepare the additional Authenticode OIDs
	oidSpOpusInfoBlob.cbData = sizeof(pbOidSpOpusInfo);
	oidSpOpusInfoBlob.pbData = pbOidSpOpusInfo;
	oidStatementTypeBlob.cbData = sizeof(pbOidStatementType);
	oidStatementTypeBlob.pbData = pbOidStatementType;
	cryptAttribute[0].cValue = 1;
	cryptAttribute[0].rgValue = &oidSpOpusInfoBlob;
	cryptAttribute[0].pszObjId = "1.3.6.1.4.1.311.2.1.12"; // SPC_SP_OPUS_INFO_OBJID in wintrust.h
	cryptAttribute[1].cValue = 1;
	cryptAttribute[1].rgValue = &oidStatementTypeBlob;
	cryptAttribute[1].pszObjId = "1.3.6.1.4.1.311.2.1.11"; // SPC_STATEMENT_TYPE_OBJID in wintrust.h
	cryptAttributesArray.cAttr = 2;
	cryptAttributesArray.rgAttr = cryptAttribute;

	// Prepare SIGNER_SIGNATURE_INFO struct
	signerSignatureInfo.cbSize = sizeof(SIGNER_SIGNATURE_INFO);
	signerSignatureInfo.algidHash = CALG_SHA1;
	signerSignatureInfo.dwAttrChoice = SIGNER_NO_ATTR;
	signerSignatureInfo.pAttrAuthcode = NULL;
	signerSignatureInfo.psAuthenticated = &cryptAttributesArray;
	signerSignatureInfo.psUnauthenticated = NULL;

	// Sign file with cert
	hResult = pfSignerSignEx(0, &signerSubjectInfo, &signerCert, &signerSignatureInfo, NULL, NULL, NULL, NULL, &pSignerContext);
	if (hResult != S_OK) {
		wdi_warn("SignerSignEx failed. hResult #%X, error %s", hResult, windows_error_str(0));
		r = WDI_ERROR_UNSIGNED;
		goto out;
	}
	r = WDI_SUCCESS;
	wdi_info("successfully signed file '%s'", szFileName);

	// Clean up
out:
	/*
	 * Because we installed our certificate as a Root CA as well as a Trusted Publisher
	 * we *MUST* ensure that the private key is destroyed, so that it cannot be reused
	 * by an attacker to self sign a malicious applications.
	 */
	if ((pCertContext != NULL) && (DeletePrivateKey(pCertContext))) {
		wdi_dbg("successfully deleted private key");
	}
	if (wszFileName != NULL) free((void*)wszFileName);
	if (pSignerContext != NULL) pfSignerFreeSignerContext(pSignerContext);
	if (pCertContext != NULL) pfCertFreeCertificateContext(pCertContext);
	if (hCertStore) pfCertCloseStore(hCertStore, 0);

	return r;
}

/*
 * Update the inf name, inf hash as well as VID/PID/MI from a libwdi .cat template
 * all filenames MUST BE LOWERCASE!!!
 */
// TODO: dynamically generated cat from CDF a la MakeCat with up to date hashes
int update_cat(char* cat_path, char* inf_path, char* usb_hwid, int driver_type)
{
	// The following constants define the byte offsets to patch in the cat templates
	const uint32_t infhash_str_pos[WDI_NB_DRIVERS-1] = {0x253, 0x937, 0x412};
	const uint32_t infhash_pos[WDI_NB_DRIVERS-1] = {0x350, 0xa34, 0x50f};
	const uint32_t infname_str_pos[WDI_NB_DRIVERS-1] = {0x3f4, 0xad8, 0x5b3};
	const uint32_t vidpidmi_str_pos[WDI_NB_DRIVERS-1] = {0xbb3, 0xed5, 0x1b20};
	PF_DECL(CryptCATOpen);
	PF_DECL(CryptCATClose);
	PF_DECL(CryptCATPersistStore);
//	PF_DECL(CryptCATStoreFromHandle);
	PF_DECL(CryptCATAdminCalcHashFromFileHandle);
	HCRYPTPROV hProv = 0;
	HANDLE hInf = NULL, hCatFile = NULL, hCatCrypt = NULL;
	LPBYTE pbCatBuffer = NULL, pbHash = NULL;
	DWORD dwCatSize, cbHash = 0;
	size_t winfname_len;
	char* inf_short;
	int i, r = WDI_ERROR_OTHER;
	wchar_t *winfname = NULL, *wcat_path = NULL;
	wchar_t wusb_hwid[] = L"vid_0000&pid_0000&mi_00";

	if ( (cat_path == NULL) || (inf_path == NULL) || (usb_hwid == NULL)
	  || (driver_type < 0) || (driver_type > WDI_NB_DRIVERS-1) ) {
		wdi_warn("illegal parameter");
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}

	PF_INIT(CryptCATOpen, wintrust);
	PF_INIT(CryptCATClose, wintrust);
	PF_INIT(CryptCATPersistStore, wintrust);
//	PF_INIT(CryptCATStoreFromHandle, wintrust);	 // TODO
	PF_INIT(CryptCATAdminCalcHashFromFileHandle, wintrust);
	if ( (pfCryptCATOpen == NULL) || (pfCryptCATClose == NULL) || (pfCryptCATPersistStore == NULL)
	  || (pfCryptCATAdminCalcHashFromFileHandle == NULL) ) {
		wdi_warn("unable to access wintrust DLL: %s", windows_error_str(0));
		r = WDI_ERROR_RESOURCE;
		goto out;
	}


	// Compute the inf hash
	hInf = CreateFileU(inf_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hInf == INVALID_HANDLE_VALUE) {
		wdi_warn("could not open file '%s'", inf_path);
		r = WDI_ERROR_ACCESS;
		goto out;
	}
	if ( (!pfCryptCATAdminCalcHashFromFileHandle(hInf, &cbHash, NULL, 0))
	  || ((pbHash = (BYTE *)malloc(cbHash)) == NULL)
	  || (!pfCryptCATAdminCalcHashFromFileHandle(hInf, &cbHash, pbHash, 0)) ) {
		wdi_warn("failed to compute hash: %s", windows_error_str(0));
		r = WDI_ERROR_IO;
		goto out;
	}

	// MS APIs: when it's A LOT easier to hack a file directly than go through 'em
	hCatFile = CreateFileU(cat_path, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	dwCatSize = GetFileSize(hCatFile, NULL);
	if ( (dwCatSize == INVALID_FILE_SIZE) || ((pbCatBuffer = (BYTE*)malloc(dwCatSize)) == NULL)
	  || (!ReadFile(hCatFile, pbCatBuffer, dwCatSize, &dwCatSize, NULL)) ) {
		wdi_warn("failed to read cat file '%s': %s", cat_path, windows_error_str(0));
		r = WDI_ERROR_ACCESS;
		goto out;
	}

	// Write the Hash, as both an UTF-16 string and as a byte hash
	for (i=0; i<20; i++) {
		_snwprintf((wchar_t*)(&pbCatBuffer[infhash_str_pos[driver_type]+4*i]), 3, L"%02X", pbHash[i]);
		pbCatBuffer[infhash_pos[driver_type]+i] = pbHash[i];
	}

	// Find the \ marker in the qualified path and write the inf name in UTF-16
	for (inf_short = &inf_path[safe_strlen(inf_path)]; (inf_short > inf_path) && (*inf_short!='\\'); inf_short--);
	inf_short++;
	winfname = utf8_to_wchar(inf_short);
	if (winfname == NULL) {
		wdi_warn("could not convert '%s' to UTF-16", inf_short);
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	winfname_len = wcslen(winfname);
	if (winfname_len > WDI_MAX_STRLEN + 4) {
		wdi_warn("'%s' is too long", inf_short);
		r = WDI_ERROR_INVALID_PARAM;
		goto out;
	}
	wcscpy((wchar_t*)(&pbCatBuffer[infname_str_pos[driver_type]]), winfname);

	// update the vid_####&pid_####[&mi_###] string
	utf8_to_wchar_no_alloc(usb_hwid, wusb_hwid, sizeof(wusb_hwid));
	_wcslwr(wusb_hwid);
	wcscpy((wchar_t*)(&pbCatBuffer[vidpidmi_str_pos[driver_type]]), wusb_hwid);

	// Save the file back
	SetFilePointer(hCatFile, 0, NULL, FILE_BEGIN);
	if (!WriteFile(hCatFile, pbCatBuffer, dwCatSize, &dwCatSize, NULL)) {
		wdi_warn("failed to write cat file '%s': %s", cat_path, windows_error_str(0));
		r = WDI_ERROR_IO;
		goto out;
	}
	CloseHandle(hCatFile);

	/*
	 * The cat hashes MUST always appear in incremental order, as the device installer
	 * fails with error 0xE000024B ("INF hash is not present in the catalog. Driver
	 * package appears to be tampered.") if the hashes are unsorted.
	 * A call to CryptCATPersistStore() does the sorting for us.
	 */
	if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
		wdi_warn("unable to acquire crypt context for CAT");
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	wcat_path = utf8_to_wchar(cat_path);
	hCatCrypt= pfCryptCATOpen(wcat_path, 0, hProv, 0, 0);
	if (hCatCrypt == INVALID_HANDLE_VALUE) {
		wdi_warn("unable to open existing cat file '%s': %s", cat_path, windows_error_str(0));
		r = WDI_ERROR_RESOURCE;
		goto out;
	}
	if (pfCryptCATPersistStore(hCatCrypt)) {
		wdi_info("successfully updated cat file");
		r = WDI_SUCCESS;
	} else {
		wdi_warn("unable to sort cat file: %s",  windows_error_str(0));
		r = WDI_ERROR_IO;
	}

out:
	if (hProv) (CryptReleaseContext(hProv,0));
	if (hInf) CloseHandle(hInf);
	if (hCatFile) CloseHandle(hCatFile);
	if (hCatCrypt) pfCryptCATClose(hCatCrypt);
	if (winfname != NULL) free(winfname);
	if (wcat_path != NULL) free(wcat_path);
	if (pbHash == NULL) free(pbHash);
	if (pbCatBuffer == NULL) free(pbCatBuffer);

	return r;
}
