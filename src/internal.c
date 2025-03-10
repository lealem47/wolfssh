/* internal.c
 *
 * Copyright (C) 2014-2022 wolfSSL Inc.
 *
 * This file is part of wolfSSH.
 *
 * wolfSSH is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfSSH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfSSH.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * The internal module contains the private data and functions. The public
 * API calls into this module to do the work of processing the connections.
 */


#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <wolfssh/ssh.h>
#include <wolfssh/internal.h>
#include <wolfssh/log.h>
#include <wolfssl/wolfcrypt/asn.h>
#ifndef WOLFSSH_NO_DH
    #include <wolfssl/wolfcrypt/dh.h>
#endif
#ifdef WOLFSSH_CERTS
    #include <wolfssl/wolfcrypt/error-crypt.h>
#endif
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/integer.h>
#include <wolfssl/wolfcrypt/signature.h>

#ifdef WOLFSSH_HAVE_LIBOQS
#include <oqs/kem.h>
#endif

#ifdef NO_INLINE
    #include <wolfssh/misc.h>
#else
    #define WOLFSSH_MISC_INCLUDED
    #include "src/misc.c"
#endif


/*
Flags:
  HAVE_WC_ECC_SET_RNG
    Set by configure if wc_ecc_set_rng() discovered in wolfCrypt.  Disables
    use of the function if the flag isn't set. If using wolfCrypt v4.5.0 or
    later, and not building with configure, set this flag.
    default: off
  WOLFSSH_NO_SHA1
    Set when SHA1 is disabled. Set to disable use of SHA1 in HMAC and digital
    signature support.
  WOLFSSH_NO_HMAC_SHA1
    Set when HMAC or SHA1 are disabled. Set to disable HMAC-SHA1 support.
  WOLFSSH_NO_HMAC_SHA1_96
    Set when HMAC or SHA1 are disabled. Set to disable HMAC-SHA1-96 support.
  WOLFSSH_NO_HMAC_SHA2_256
    Set when HMAC or SHA2-256 are disabled. Set to disable HMAC-SHA2-256
    support.
  WOLFSSH_NO_DH_GROUP1_SHA1
    Set when DH or SHA1 are disabled. Set to disable use of DH (Oakley 1) and
    SHA1 support.
  WOLFSSH_NO_DH_GROUP14_SHA1
    Set when DH or SHA1 are disabled. Set to disable use of DH (Oakley 14) and
    SHA1 support.
  WOLFSSH_NO_DH_GEX_SHA256
    Set when DH or SHA2-256 are disabled. Set to disable use of DH group
    exchange and SHA2-256 support.
  WOLFSSH_NO_ECDH_SHA2_NISTP256
    Set when ECC or SHA2-256 are disabled. Set to disable use of ECDHE key
    exchange with prime NISTP256.
  WOLFSSH_NO_ECDH_SHA2_NISTP384
    Set when ECC or SHA2-384 are disabled. Set to disable use of ECDHE key
    exchange with prime NISTP384.
  WOLFSSH_NO_ECDH_SHA2_NISTP521
    Set when ECC or SHA2-512 are disabled. Set to disable use of ECDHE key
    exchange with prime NISTP521.
  WOLFSSH_NO_ECDH_SHA2_ED25519
    Set when ED25519 or SHA2-256 are disabled. Set to disable use of ECDHE key
    exchange with prime ED25519. (It just decodes the ID for output.)
  WOLFSSH_NO_RSA
    Set when RSA is disabled. Set to disable use of RSA server and user
    authentication.
  WOLFSSH_NO_SSH_RSA_SHA1
    Set when RSA or SHA1 are disabled. Set to disable use of RSA server
    authentication.
  WOLFSSH_NO_ECDSA
    Set when ECC is disabled. Set to disable use of ECDSA server and user
    authentication.
  WOLFSSH_NO_ECDSA_SHA2_NISTP256
    Set when ECC or SHA2-256 are disabled. Set to disable use of ECDSA server
    authentication with prime NISTP256.
  WOLFSSH_NO_ECDSA_SHA2_NISTP384
    Set when ECC or SHA2-384 are disabled. Set to disable use of ECDSA server
    authentication with prime NISTP384.
  WOLFSSH_NO_ECDSA_SHA2_NISTP521
    Set when ECC or SHA2-512 are disabled. Set to disable use of ECDSA server
    authentication with prime NISTP521.
  WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
    Set when there is no liboqs integration. Set to disable use of ECDHE with
    prime NISTP256 hybridized with post-quantum Kyber Level1 KEM.
  WOLFSSH_NO_AES_CBC
    Set when AES or AES-CBC are disabled. Set to disable use of AES-CBC
    encryption.
  WOLFSSH_NO_AES_CTR
    Set when AES or AES-CTR are disabled. Set to disable use of AES-CTR
    encryption.
  WOLFSSH_NO_AES_GCM
    Set when AES or AES-GCM are disabled. Set to disable use of AES-GCM
    encryption.
  WOLFSSH_NO_AEAD
    Set when AES-GCM is disabled. Set to disable use of AEAD ciphers for
    encryption. Setting this will force all AEAD ciphers off.
  WOLFSSH_NO_DH
    Set when all DH algorithms are disabled. Set to disable use of all DH
    algorithms for key agreement. Setting this will force all DH key agreement
    algorithms off.
  WOLFSSH_NO_ECDH
    Set when all ECDH algorithms are disabled. Set to disable use of all ECDH
    algorithms for key agreement. Setting this will force all ECDH key agreement
    algorithms off.
*/

static int SetHostPrivateKey(WOLFSSH_CTX* ctx, byte keyId, int isKey,
        byte* der, word32 derSz, int dynamicType);

static const char sshProtoIdStr[] = "SSH-2.0-wolfSSHv"
                                    LIBWOLFSSH_VERSION_STRING
                                    "\r\n";
static const char OpenSSH[] = "SSH-2.0-OpenSSH";


const char* GetErrorString(int err)
{
#ifdef NO_WOLFSSH_STRINGS
    WOLFSSH_UNUSED(err);
    return "No wolfSSH strings available";
#else
    switch (err) {
        case WS_ERROR:
            return "general function failure";

        case WS_BAD_ARGUMENT:
            return "bad function argument";

        case WS_MEMORY_E:
            return "memory allocation failure";

        case WS_BUFFER_E:
            return "input/output buffer size error";

        case WS_PARSE_E:
            return "general parsing error";

        case WS_NOT_COMPILED:
            return "feature not compiled in";

        case WS_OVERFLOW_E:
            return "would overflow if continued failure";

        case WS_BAD_USAGE:
            return "bad example usage";

        case WS_SOCKET_ERROR_E:
            return "socket error";

        case WS_WANT_READ:
            return "I/O callback would read block error";

        case WS_WANT_WRITE:
            return "I/O callback would write block error";

        case WS_RECV_OVERFLOW_E:
            return "receive buffer overflow";

        case WS_VERSION_E:
            return "peer version unsupported";

        case WS_SEND_OOB_READ_E:
            return "attempted to read buffer out of bounds";

        case WS_INPUT_CASE_E:
            return "bad process input state, programming error";

        case WS_BAD_FILETYPE_E:
            return "bad filetype";

        case WS_UNIMPLEMENTED_E:
            return "feature not implemented";

        case WS_RSA_E:
            return "RSA buffer error";

        case WS_BAD_FILE_E:
            return "bad file";

        case WS_INVALID_ALGO_ID:
            return "invalid algorithm id";

        case WS_DECRYPT_E:
            return "decrypt error";

        case WS_ENCRYPT_E:
            return "encrypt error";

        case WS_VERIFY_MAC_E:
            return "verify mac error";

        case WS_CREATE_MAC_E:
            return "create mac error";

        case WS_RESOURCE_E:
            return "insufficient resources for new channel";

        case WS_INVALID_CHANTYPE:
            return "peer requested invalid channel type";

        case WS_INVALID_CHANID:
            return "peer requested invalid channel id";

        case WS_INVALID_USERNAME:
            return "invalid user name";

        case WS_CRYPTO_FAILED:
            return "crypto action failed";

        case WS_INVALID_STATE_E:
            return "invalid state";

        case WS_EOF:
            return "end of file";

        case WS_REKEYING:
            return "rekeying with peer";

        case WS_INVALID_PRIME_CURVE:
            return "invalid prime curve in ecc";

        case WS_ECC_E:
            return "ECDSA buffer error";

        case WS_CHANOPEN_FAILED:
            return "peer returned channel open failure";

        case WS_CHANNEL_CLOSED:
            return "channel closed";

        case WS_INVALID_PATH_E:
            return "invalid file or directory path";

        case WS_SCP_CMD_E:
            return "invalid scp command";

        case WS_SCP_BAD_MSG_E:
            return "invalid scp message received from peer";

        case WS_SCP_PATH_LEN_E:
            return "scp path length error";

        case WS_SCP_TIMESTAMP_E:
            return "scp timestamp message error";

        case WS_SCP_DIR_STACK_EMPTY_E:
            return "scp directory stack empty";

        case WS_SCP_CONTINUE:
            return "scp continue operation";

        case WS_SCP_ABORT:
            return "scp abort operation";

        case WS_SCP_ENTER_DIR:
            return "scp enter directory operation";

        case WS_SCP_EXIT_DIR:
            return "scp exit directory operation";

        case WS_SCP_EXIT_DIR_FINAL:
            return "scp final exit directory operation";

        case WS_SCP_COMPLETE:
            return "scp operation complete";

        case WS_SCP_INIT:
            return "scp operation verified";

        case WS_MATCH_KEX_ALGO_E:
            return "cannot match KEX algo with peer";

        case WS_MATCH_KEY_ALGO_E:
            return "cannot match key algo with peer";

        case WS_MATCH_ENC_ALGO_E:
            return "cannot match encrypt algo with peer";

        case WS_MATCH_MAC_ALGO_E:
            return "cannot match MAC algo with peer";

        case WS_PERMISSIONS:
            return "file permissions error";

        case WS_SFTP_COMPLETE:
            return "sftp connection established";

        case WS_NEXT_ERROR:
            return "Getting next value/state results in error";

        case WS_CHAN_RXD:
            return "Channel data received";

        case WS_INVALID_EXTDATA:
            return "invalid extended data type";

        case WS_SFTP_BAD_REQ_ID:
            return "sftp bad request id";

        case WS_SFTP_BAD_REQ_TYPE:
            return "sftp bad request response type";

        case WS_SFTP_STATUS_NOT_OK:
            return "sftp status not OK";

        case WS_SFTP_FILE_DNE:
            return "sftp file does not exist";

        case WS_SIZE_ONLY:
            return "Only getting the size of buffer needed";

        case WS_CLOSE_FILE_E:
            return "Unable to close local file";

        case WS_PUBKEY_REJECTED_E:
            return "server's public key is rejected";

        case WS_EXTDATA:
            return "Extended Data available to be read";

        case WS_USER_AUTH_E:
            return "User authentication error";

        case WS_SSH_NULL_E:
            return "ssh pointer was null";

        case WS_SSH_CTX_NULL_E:
            return "ssh ctx pointer was null";

        case WS_CHANNEL_NOT_CONF:
            return "channel open not confirmed";

        case WS_CHANGE_AUTH_E:
            return "changing auth type attempt";

        case WS_WINDOW_FULL:
            return "peer's channel window full";

        case WS_MISSING_CALLBACK:
            return "missing a callback function";

        case WS_DH_SIZE_E:
            return "DH prime group size larger than expected";

        case WS_PUBKEY_SIG_MIN_E:
            return "pubkey signature too small";

        case WS_AGENT_NULL_E:
            return "agent pointer was null";

        case WS_AGENT_NO_KEY_E:
            return "agent doesn't have requested key";

        case WS_AGENT_CXN_FAIL:
            return "agent connection failed";

        case WS_SFTP_BAD_HEADER:
            return "sftp bad header";

        case WS_CERT_NO_SIGNER_E:
            return "no signer certificate";

        case WS_CERT_EXPIRED_E:
            return "certificate expired";

        case WS_CERT_REVOKED_E:
            return "certificate revoked";

        case WS_CERT_SIG_CONFIRM_E:
            return "certificate signature fail";

        case WS_CERT_OTHER_E:
            return "other certificate error";

        case WS_CERT_PROFILE_E:
            return "certificate profile requirements error";

        case WS_CERT_KEY_SIZE_E:
            return "key size too small error";

        case WS_CTX_KEY_COUNT_E:
            return "trying to add too many keys";

        default:
            return "Unknown error code";
    }
#endif
}


static int wsHighwater(byte dir, void* ctx)
{
    int ret = WS_SUCCESS;

    WOLFSSH_UNUSED(dir);

    if (ctx) {
        WOLFSSH* ssh = (WOLFSSH*)ctx;

        WLOG(WS_LOG_DEBUG, "HIGHWATER MARK: (%u) %s",
             wolfSSH_GetHighwater(ssh),
             (dir == WOLFSSH_HWSIDE_RECEIVE) ? "receive" : "transmit");

        ret = wolfSSH_TriggerKeyExchange(ssh);
    }

    return ret;
}


/* internal abstract function for hash update
 * returns 0 on success */
static int HashUpdate(wc_HashAlg* hash, enum wc_HashType type,
    const byte* data, word32 dataSz)
{
#if 0
    word32 i;
    printf("Hashing In :");
    for (i = 0; i < dataSz; i++)
        printf("%02X", data[i]);
    printf("\n");
#endif
    return wc_HashUpdate(hash, type, data, dataSz);
}


/* returns WS_SUCCESS on success */
static INLINE int HighwaterCheck(WOLFSSH* ssh, byte side)
{
    int ret = WS_SUCCESS;

    if (!ssh->highwaterFlag && ssh->highwaterMark &&
        (ssh->txCount >= ssh->highwaterMark ||
         ssh->rxCount >= ssh->highwaterMark)) {

        WLOG(WS_LOG_DEBUG, "%s over high water mark",
             (side == WOLFSSH_HWSIDE_TRANSMIT) ? "Transmit" : "Receive");

        ssh->highwaterFlag = 1;

        if (ssh->ctx->highwaterCb)
            ret = ssh->ctx->highwaterCb(side, ssh->highwaterCtx);
    }
    return ret;
}


static HandshakeInfo* HandshakeInfoNew(void* heap)
{
    HandshakeInfo* newHs;

    WLOG(WS_LOG_DEBUG, "Entering HandshakeInfoNew()");
    newHs = (HandshakeInfo*)WMALLOC(sizeof(HandshakeInfo),
                                    heap, DYNTYPE_HS);
    if (newHs != NULL) {
        WMEMSET(newHs, 0, sizeof(HandshakeInfo));
        newHs->kexId = ID_NONE;
        newHs->pubKeyId  = ID_NONE;
        newHs->sigId  = ID_NONE;
        newHs->encryptId = ID_NONE;
        newHs->macId = ID_NONE;
        newHs->blockSz = MIN_BLOCK_SZ;
        newHs->hashId = WC_HASH_TYPE_NONE;
        newHs->eSz = sizeof newHs->e;
        newHs->xSz = sizeof newHs->x;
#ifndef WOLFSSH_NO_DH_GEX_SHA256
        newHs->dhGexMinSz = WOLFSSH_DEFAULT_GEXDH_MIN;
        newHs->dhGexPreferredSz = WOLFSSH_DEFAULT_GEXDH_PREFERRED;
        newHs->dhGexMaxSz = WOLFSSH_DEFAULT_GEXDH_MAX;
#endif
    }

    return newHs;
}


static void HandshakeInfoFree(HandshakeInfo* hs, void* heap)
{
    WOLFSSH_UNUSED(heap);

    WLOG(WS_LOG_DEBUG, "Entering HandshakeInfoFree()");
    if (hs) {
        WFREE(hs->kexInit, heap, DYNTYPE_STRING);
#ifndef WOLFSSH_NO_DH
        WFREE(hs->primeGroup, heap, DYNTYPE_MPINT);
        WFREE(hs->generator, heap, DYNTYPE_MPINT);
#endif
        if (hs->hashId != WC_HASH_TYPE_NONE)
            wc_HashFree(&hs->hash, (enum wc_HashType)hs->hashId);
        ForceZero(hs, sizeof(HandshakeInfo));
        WFREE(hs, heap, DYNTYPE_HS);
    }
}


#ifdef DEBUG_WOLFSSH

static const char cannedBanner[] =
    "CANNED BANNER\r\n"
    "This server is an example test server. "
    "It should have its own banner, but\r\n"
    "it is currently using a canned one in "
    "the library. Be happy or not.\r\n";
static const word32 cannedBannerSz = sizeof(cannedBanner) - 1;

#endif /* DEBUG_WOLFSSH */


WOLFSSH_CTX* CtxInit(WOLFSSH_CTX* ctx, byte side, void* heap)
{
    word32 idx;

    WLOG(WS_LOG_DEBUG, "Entering CtxInit()");

    if (ctx == NULL)
        return ctx;

    WMEMSET(ctx, 0, sizeof(WOLFSSH_CTX));

    if (heap)
        ctx->heap = heap;

    ctx->side = side;
#ifndef WOLFSSH_USER_IO
    ctx->ioRecvCb = wsEmbedRecv;
    ctx->ioSendCb = wsEmbedSend;
#endif /* WOLFSSH_USER_IO */
    ctx->highwaterMark = DEFAULT_HIGHWATER_MARK;
    ctx->highwaterCb = wsHighwater;
#if defined(WOLFSSH_SCP) && !defined(WOLFSSH_SCP_USER_CALLBACKS)
    ctx->scpRecvCb = wsScpRecvCallback;
    ctx->scpSendCb = wsScpSendCallback;
#endif /* WOLFSSH_SCP */
#ifdef DEBUG_WOLFSSH
    ctx->banner = cannedBanner;
    ctx->bannerSz = cannedBannerSz;
#endif /* DEBUG_WOLFSSH */
#ifdef WOLFSSH_CERTS
    ctx->certMan = wolfSSH_CERTMAN_new(ctx->heap);
    if (ctx->certMan == NULL)
        return NULL;
#endif /* WOLFSSH_CERTS */
    ctx->windowSz = DEFAULT_WINDOW_SZ;
    ctx->maxPacketSz = DEFAULT_MAX_PACKET_SZ;

    for (idx = 0; idx < WOLFSSH_MAX_PVT_KEYS; idx++) {
        ctx->privateKeyId[idx] = ID_NONE;
    }

    return ctx;
}


void CtxResourceFree(WOLFSSH_CTX* ctx)
{
    WLOG(WS_LOG_DEBUG, "Entering CtxResourceFree()");

    if (ctx->privateKeyCount > 0) {
        word32 i;

        for (i = 0; i < ctx->privateKeyCount; i++) {
            if (ctx->privateKey[i] != NULL) {
                ForceZero(ctx->privateKey[i], ctx->privateKeySz[i]);
                WFREE(ctx->privateKey[i], ctx->heap, DYNTYPE_PRIVKEY);
                ctx->privateKey[i] = NULL;
                ctx->privateKeySz[i] = 0;
            }
            #ifdef WOLFSSH_CERTS
            if (ctx->cert[i] != NULL) {
                WFREE(ctx->cert[i], ctx->heap, DYNTYPE_CERT);
                ctx->cert[i] = NULL;
                ctx->certSz[i] = 0;
            }
            #endif
            ctx->privateKeyId[i] = ID_NONE;
        }
        ctx->privateKeyCount = 0;
    }
#ifdef WOLFSSH_CERTS
    if (ctx->certMan) {
        wolfSSH_CERTMAN_free(ctx->certMan);
    }
    ctx->certMan = NULL;
#endif
}


/* check for cases where a public key is a certificate and the private key
 * should be marked for use with X509 */
static void UpdateKeyID(WOLFSSH_CTX* ctx)
{
#ifdef WOLFSSH_CERTS
    word32 idx;

    for (idx = 0; idx < ctx->privateKeyCount &&
                  idx < WOLFSSH_MAX_PVT_KEYS; idx++) {
        if (ctx->cert[idx] != NULL && ctx->certSz[idx] > 0) {
            byte keyId;
            byte* der;

            /* matching certificate was set, convert private key id */
            keyId = ctx->privateKeyId[idx];
            switch (keyId) {
            #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
                case ID_ECDSA_SHA2_NISTP521:
                    ctx->privateKeyId[idx] = ID_X509V3_ECDSA_SHA2_NISTP521;
                    break;
            #endif
            #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
                case ID_ECDSA_SHA2_NISTP384:
                    ctx->privateKeyId[idx] = ID_X509V3_ECDSA_SHA2_NISTP384;
                    break;
            #endif
            #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
                case ID_ECDSA_SHA2_NISTP256:
                    ctx->privateKeyId[idx] = ID_X509V3_ECDSA_SHA2_NISTP256;
                    break;
            #endif
            #ifndef WOLFSSH_NO_SSH_RSA_SHA1
                case ID_SSH_RSA:
                    ctx->privateKeyId[idx] = ID_X509V3_SSH_RSA;
                    break;
            #endif
            }

            /* can use the key for non X509v3 connections too */
            der = (byte*)WMALLOC(ctx->privateKeySz[idx], ctx->heap,
                DYNTYPE_PRIVKEY);
            if (der != NULL) {
                int ret;
                WMEMCPY(der, ctx->privateKey[idx], ctx->privateKeySz[idx]);
                ret = SetHostPrivateKey(ctx, keyId, 1, der,
                            ctx->privateKeySz[idx], DYNTYPE_PRIVKEY);
                if (ret != 0) {
                    WFREE(der, ctx->heap, DYNTYPE_PRIVKEY);
                }
            }
        }
    }
#endif
    WOLFSSH_UNUSED(ctx);
}


WOLFSSH* SshInit(WOLFSSH* ssh, WOLFSSH_CTX* ctx)
{
#if defined(STM32F2) || defined(STM32F4) || defined(FREESCALE_MQX)
    /* avoid name conflict in "stm32fnnnxx.h" */
    #undef  RNG
    #define RNG WC_RNG
#endif
    HandshakeInfo* handshake;
    WC_RNG*        rng;
    void*          heap;

    WLOG(WS_LOG_DEBUG, "Entering SshInit()");

    if (ssh == NULL || ctx == NULL)
        return ssh;
    heap = ctx->heap;

    handshake = HandshakeInfoNew(heap);
    rng = (WC_RNG*)WMALLOC(sizeof(WC_RNG), heap, DYNTYPE_RNG);

    if (handshake == NULL || rng == NULL || wc_InitRng(rng) != 0) {

        WLOG(WS_LOG_DEBUG, "SshInit: Cannot allocate memory.\n");
        WFREE(handshake, heap, DYNTYPE_HS);
        WFREE(rng, heap, DYNTYPE_RNG);
        WFREE(ssh, heap, DYNTYPE_SSH);
        return NULL;
    }

    WMEMSET(ssh, 0, sizeof(WOLFSSH));  /* default init to zeros */

    UpdateKeyID(ctx); /* set IDs before use */
    ssh->ctx         = ctx;
    ssh->error       = WS_SUCCESS;
#ifdef USE_WINDOWS_API
    ssh->rfd         = INVALID_SOCKET;
    ssh->wfd         = INVALID_SOCKET;
#else
    ssh->rfd         = -1;         /* set to invalid */
    ssh->wfd         = -1;         /* set to invalid */
#endif
    ssh->ioReadCtx   = &ssh->rfd;  /* prevent invalid access if not correctly */
    ssh->ioWriteCtx  = &ssh->wfd;  /* set */
    ssh->highwaterMark = ctx->highwaterMark;
    ssh->highwaterCtx  = (void*)ssh;
    ssh->reqSuccessCtx = (void*)ssh;
    ssh->fs            = NULL;
    ssh->acceptState = ACCEPT_BEGIN;
    ssh->clientState = CLIENT_BEGIN;
    ssh->isKeying    = 1;
    ssh->authId      = ID_USERAUTH_PUBLICKEY;
    ssh->supportedAuth[0] = ID_USERAUTH_PUBLICKEY;
    ssh->supportedAuth[1] = ID_USERAUTH_PASSWORD;
    ssh->supportedAuth[2] = ID_NONE; /* ID_NONE is treated as empty slot */
    ssh->nextChannel = DEFAULT_NEXT_CHANNEL;
    ssh->blockSz     = MIN_BLOCK_SZ;
    ssh->encryptId   = ID_NONE;
    ssh->macId       = ID_NONE;
    ssh->peerBlockSz = MIN_BLOCK_SZ;
    ssh->rng         = rng;
    ssh->kSz         = sizeof(ssh->k);
    ssh->handshake   = handshake;
    ssh->connectChannelId = WOLFSSH_SESSION_SHELL;
#ifdef WOLFSSH_SCP
    ssh->scpRequestState = SCP_PARSE_COMMAND;
    ssh->scpConfirmMsg   = NULL;
    ssh->scpConfirmMsgSz = 0;
    ssh->scpRecvCtx      = NULL;
    #if !defined(WOLFSSH_SCP_USER_CALLBACKS) && !defined(NO_FILESYSTEM)
    ssh->scpSendCtx      = &(ssh->scpSendCbCtx);
    #else
    ssh->scpSendCtx      = NULL;
    #endif
    ssh->scpFileBuffer   = NULL;
    ssh->scpFileBufferSz = 0;
    ssh->scpFileName     = NULL;
    ssh->scpFileNameSz   = 0;
    ssh->scpTimestamp    = 0;
    ssh->scpATime        = 0;
    ssh->scpMTime        = 0;
    ssh->scpRequestType  = WOLFSSH_SCP_SINGLE_FILE_REQUEST;
    ssh->scpIsRecursive  = 0;
    ssh->scpDirection    = WOLFSSH_SCP_DIR_NONE;
#endif

#ifdef WOLFSSH_SFTP
    ssh->sftpState   = SFTP_BEGIN;
#endif

#ifdef WOLFSSH_AGENT
    ssh->agentEnabled = ctx->agentEnabled;
#endif

    if (BufferInit(&ssh->inputBuffer, 0, ctx->heap) != WS_SUCCESS  ||
        BufferInit(&ssh->outputBuffer, 0, ctx->heap) != WS_SUCCESS ||
        BufferInit(&ssh->extDataBuffer, 0, ctx->heap) != WS_SUCCESS) {

        wolfSSH_free(ssh);
        ssh = NULL;
    }

    return ssh;
}


void SshResourceFree(WOLFSSH* ssh, void* heap)
{
    /* when ssh holds resources, free here */
    WOLFSSH_UNUSED(heap);

    WLOG(WS_LOG_DEBUG, "Entering sshResourceFree()");

    ShrinkBuffer(&ssh->inputBuffer, 1);
    ShrinkBuffer(&ssh->outputBuffer, 1);
    ShrinkBuffer(&ssh->extDataBuffer, 1);
    ForceZero(ssh->k, ssh->kSz);
    HandshakeInfoFree(ssh->handshake, heap);
    ForceZero(&ssh->keys, sizeof(Keys));
    ForceZero(&ssh->peerKeys, sizeof(Keys));
    if (ssh->rng) {
        wc_FreeRng(ssh->rng);
        WFREE(ssh->rng, heap, DYNTYPE_RNG);
    }
    if (ssh->userName) {
        WFREE(ssh->userName, heap, DYNTYPE_STRING);
    }
    if (ssh->peerProtoId) {
        WFREE(ssh->peerProtoId, heap, DYNTYPE_STRING);
    }
    if (ssh->channelList) {
        WOLFSSH_CHANNEL* cur = ssh->channelList;
        WOLFSSH_CHANNEL* next;
        while (cur) {
            next = cur->next;
            ChannelDelete(cur, heap);
            cur = next;
        }
    }
    wc_AesFree(&ssh->encryptCipher.aes);
    wc_AesFree(&ssh->decryptCipher.aes);

#ifdef WOLFSSH_SCP
    if (ssh->scpConfirmMsg) {
        WFREE(ssh->scpConfirmMsg, ssh->ctx->heap, DYNTYPE_STRING);
        ssh->scpConfirmMsg = NULL;
        ssh->scpConfirmMsgSz = 0;
    }
    if (ssh->scpFileBuffer) {
        ForceZero(ssh->scpFileBuffer, ssh->scpFileBufferSz);
        WFREE(ssh->scpFileBuffer, ssh->ctx->heap, DYNTYPE_BUFFER);
        ssh->scpFileBuffer = NULL;
        ssh->scpFileBufferSz = 0;
    }
    if (ssh->scpFileName) {
        WFREE(ssh->scpFileName, ssh->ctx->heap, DYNTYPE_STRING);
        ssh->scpFileName = NULL;
        ssh->scpFileNameSz = 0;
    }
    if (ssh->scpRecvMsg) {
        WFREE(ssh->scpRecvMsg, ssh->heap, DYNTYPE_STRING);
        ssh->scpRecvMsg = NULL;
        ssh->scpRecvMsgSz = 0;
    }
#ifdef WOLFSSL_NUCLEUS
    WFREE(ssh->scpBasePathDynamic, ssh->ctx->heap, DYNTYPE_BUFFER);
    ssh->scpBasePathDynamic = NULL;
    ssh->scpBasePathSz = 0;
#endif
#endif
#ifdef WOLFSSH_SFTP
    if (ssh->sftpDefaultPath) {
        WFREE(ssh->sftpDefaultPath, ssh->ctx->heap, DYNTYPE_STRING);
        ssh->sftpDefaultPath = NULL;
    }
#endif
}

union wolfSSH_key {
#ifndef WOLFSSH_NO_RSA
    RsaKey rsa;
#endif
#ifndef WOLFSSH_NO_ECDSA
    ecc_key ecc;
#endif
};


/*
 * Identifies the flavor of a key, RSA or ECDSA, and returns the key type ID.
 * The process is to decode the key as if it was RSA and if that fails try
 * to load it as if ECDSA. Both public and private keys can be decoded.
 *
 * @param in        key to identify
 * @param inSz      size of key
 * @param isPrivate indicates private or public key
 * @param heap      heap to use for memory allocation
 * @return          keyId as int, WS_MEMORY_E, WS_UNIMPLEMENTED_E
 */
int IdentifyKey(const byte* in, word32 inSz, int isPrivate, void* heap)
{
    union wolfSSH_key *key = NULL;
    int keyId = ID_UNKNOWN;
    word32 idx;
    int ret;
    int dynType = isPrivate ? DYNTYPE_PRIVKEY : DYNTYPE_PUBKEY;

    key = (union wolfSSH_key*)WMALLOC(sizeof(union wolfSSH_key), heap, dynType);

#ifndef WOLFSSH_NO_RSA
    if (key != NULL) {
        /* Check RSA key */
        if (keyId == ID_UNKNOWN) {
            idx = 0;
            ret = wc_InitRsaKey(&key->rsa, NULL);

            if (ret == 0) {
                if (isPrivate) {
                    ret = wc_RsaPrivateKeyDecode(in, &idx, &key->rsa, inSz);
                }
                else {
                    ret = wc_RsaPublicKeyDecode(in, &idx, &key->rsa, inSz);
                }

                /* If decode was successful, this is an RSA key. */
                if (ret == 0) {
                    keyId = ID_SSH_RSA;
                }
            }

            wc_FreeRsaKey(&key->rsa);
        }
    }
#endif
#ifndef WOLFSSH_NO_ECDSA
    if (key != NULL) {
        /* Check ECDSA key */
        if (keyId == ID_UNKNOWN) {
            idx = 0;
            ret = wc_ecc_init_ex(&key->ecc, heap, INVALID_DEVID);

            if (ret == 0) {
                if (isPrivate) {
                    ret = wc_EccPrivateKeyDecode(in, &idx, &key->ecc, inSz);
                }
                else {
                    ret = wc_EccPublicKeyDecode(in, &idx, &key->ecc, inSz);
                }

                /* If decode was successful, this is an ECDSA key. */
                if (ret == 0) {
                    switch (wc_ecc_get_curve_id(key->ecc.idx)) {
                        case ECC_SECP256R1:
                            keyId = ID_ECDSA_SHA2_NISTP256;
                            break;
                        case ECC_SECP384R1:
                            keyId = ID_ECDSA_SHA2_NISTP384;
                            break;
                        case ECC_SECP521R1:
                            keyId = ID_ECDSA_SHA2_NISTP521;
                            break;
                    }
                }
            }

            wc_ecc_free(&key->ecc);
        }
    }
#endif /* ! WOLFSSH_NO_ECDSA */

    if (key == NULL) {
        ret = WS_MEMORY_E;
    }
    else if (keyId == ID_UNKNOWN) {
        ret = WS_UNIMPLEMENTED_E;
    }
    else {
        ret = keyId;
    }
    WFREE(key, heap, dynType);

    return ret;
}


#ifdef WOLFSSH_CERTS
/*
 * Identifies the flavor of an X.509 certificate, RSA or ECDSA, and returns
 * the key type ID. The process is to decode the certificate and pass the
 * public key to IdentifyKey.
 *
 * @param in        certificate to identify
 * @param inSz      size of certificate
 * @param heap      heap to use for memory allocation
 * @return          keyId as int, WS_MEMORY_E, WS_UNIMPLEMENTED_E
 */
static int IdentifyCert(const byte* in, word32 inSz, void* heap)
{
    struct DecodedCert* cert = NULL;
#ifndef WOLFSSH_SMALL_STACK
    struct DecodedCert cert_s;
#endif
    byte *key = NULL;
    word32 keySz = 0;
    int ret = 0;

#ifndef WOLFSSH_SMALL_STACK
    cert = &cert_s;
#else
    cert = (struct DecodedCert*)WMALLOC(sizeof(struct DecodedCert),
            heap, DYNTYPE_CERT);
    if (cert == NULL) {
        ret = WS_MEMORY_E;
    }
#endif

    if (ret == 0) {
        wc_InitDecodedCert(cert, in, inSz, heap);
        ret = wc_ParseCert(cert, CERT_TYPE, 0, NULL);
    }
    if (ret == 0) {
        ret = wc_GetPubKeyDerFromCert(cert, NULL, &keySz);
        if (ret == LENGTH_ONLY_E) {
            ret = 0;
            key = (byte*)WMALLOC(keySz, heap, DYNTYPE_PUBKEY);
            if (key == NULL) {
                ret = WS_MEMORY_E;
            }
        }
    }

    if (ret == 0) {
        ret = wc_GetPubKeyDerFromCert(cert, key, &keySz);
    }

    if (ret == 0) {
        ret = IdentifyKey(key, keySz, 0, heap);
    }

    WFREE(key, heap, DYNTYPE_PUBKEY);
    if (cert != NULL) {
        wc_FreeDecodedCert(cert);
        #ifdef WOLFSSH_SMALL_STACK
            WFREE(cert, heap, DYNTYPE_CERT);
        #endif
    }

    return ret;
}
#endif /* WOLFSSH_CERTS */


int SetHostPrivateKey(WOLFSSH_CTX* ctx, byte keyId, int isKey,
        byte* der, word32 derSz, int dynamicType)
{
    word32 destIdx = 0;
    int ret = WS_SUCCESS;

    WOLFSSH_UNUSED(dynamicType);

    while (destIdx < ctx->privateKeyCount &&
            ctx->privateKeyId[destIdx] != keyId) {
        destIdx++;
    }

    if (destIdx >= WOLFSSH_MAX_PVT_KEYS) {
        ret = WS_CTX_KEY_COUNT_E;
    }
    else {
        if (ctx->privateKeyId[destIdx] == keyId) {
            if (isKey) {
                if (ctx->privateKey[destIdx] != NULL) {
                    ForceZero(ctx->privateKey[destIdx],
                            ctx->privateKeySz[destIdx]);
                    WFREE(ctx->privateKey[destIdx], heap, dynamicType);
                }
            }
            #ifdef WOLFSSH_CERTS
            else {
                if (ctx->cert[destIdx] != NULL) {
                    WFREE(ctx->cert[destIdx], heap, dynamicType);
                }
            }
            #endif /* WOLFSSH_CERTS */
        }
        else {
            ctx->privateKeyCount++;
            ctx->privateKeyId[destIdx] = keyId;
        }

        if (isKey) {
            ctx->privateKey[destIdx] = der;
            ctx->privateKeySz[destIdx] = derSz;
        }
        #ifdef WOLFSSH_CERTS
        else {
            ctx->cert[destIdx] = der;
            ctx->certSz[destIdx] = derSz;
        }
        #endif /* WOLFSSH_CERTS */
    }

    return ret;
}


int wolfSSH_ProcessBuffer(WOLFSSH_CTX* ctx,
                          const byte* in, word32 inSz,
                          int format, int type)
{
    void* heap = NULL;
    byte* der;
    word32 derSz;
    int wcType;
    int ret = WS_SUCCESS;
    int dynamicType = 0;
    byte keyId = ID_NONE;

    if (ctx == NULL || in == NULL || inSz == 0)
        return WS_BAD_ARGUMENT;

    if (format != WOLFSSH_FORMAT_ASN1 && format != WOLFSSH_FORMAT_PEM &&
                                         format != WOLFSSH_FORMAT_RAW)
        return WS_BAD_FILETYPE_E;

    if (type == BUFTYPE_CA) {
        dynamicType = DYNTYPE_CA;
        wcType = CA_TYPE;
    }
    else if (type == BUFTYPE_CERT) {
        dynamicType = DYNTYPE_CERT;
        wcType = CERT_TYPE;
    }
    else if (type == BUFTYPE_PRIVKEY) {
        dynamicType = DYNTYPE_PRIVKEY;
        wcType = PRIVATEKEY_TYPE;
    }
    else {
        return WS_BAD_ARGUMENT;
    }

    heap = ctx->heap;

    if (format == WOLFSSH_FORMAT_ASN1 || format == WOLFSSH_FORMAT_RAW) {
        if (in[0] != 0x30)
            return WS_BAD_FILETYPE_E;
        der = (byte*)WMALLOC(inSz, heap, dynamicType);
        if (der == NULL)
            return WS_MEMORY_E;
        WMEMCPY(der, in, inSz);
        derSz = inSz;
    }
    #ifdef WOLFSSH_CERTS
    else if (format == WOLFSSH_FORMAT_PEM) {
        /* The der size will be smaller than the pem size. */
        der = (byte*)WMALLOC(inSz, heap, dynamicType);
        if (der == NULL)
            return WS_MEMORY_E;

        ret = wc_CertPemToDer(in, inSz, der, inSz, wcType);
        if (ret < 0) {
            WFREE(der, heap, dynamicType);
            return WS_BAD_FILE_E;
        }
        derSz = (word32)ret;
    }
    #endif /* WOLFSSH_CERTS */
    else {
        return WS_UNIMPLEMENTED_E;
    }

    /* Maybe decrypt */

    if (type == BUFTYPE_PRIVKEY) {
        ret = IdentifyKey(der, derSz, 1, ctx->heap);
        if (ret < 0) {
            WFREE(der, heap, dynamicType);
            return ret;
        }
        keyId = (byte)ret;
        ret = SetHostPrivateKey(ctx, keyId, 1, der, derSz, dynamicType);
    }
    #ifdef WOLFSSH_CERTS
    else if (type == BUFTYPE_CERT) {
        ret = IdentifyCert(der, derSz, ctx->heap);
        if (ret < 0) {
            WFREE(der, heap, dynamicType);
            return ret;
        }
        keyId = (byte)ret;
        ret = SetHostPrivateKey(ctx, keyId, 0, der, derSz, dynamicType);
    }
    else if (type == BUFTYPE_CA) {
        if (ctx->certMan != NULL) {
            ret = wolfSSH_CERTMAN_LoadRootCA_buffer(ctx->certMan, der, derSz);
        }
        else {
            WLOG(WS_LOG_DEBUG, "Error no cert manager set");
            ret = WS_MEMORY_E;
        }
        WFREE(der, heap, dynamicType);
        if (ret < 0) {
            WLOG(WS_LOG_DEBUG, "Error %d loading in CA buffer", ret);
        }
    }
    #endif /* WOLFSSH_CERTS */

    WOLFSSH_UNUSED(dynamicType);
    WOLFSSH_UNUSED(wcType);
    WOLFSSH_UNUSED(heap);

    return ret;
}


int GenerateKey(byte hashId, byte keyId,
                byte* key, word32 keySz,
                const byte* k, word32 kSz,
                const byte* h, word32 hSz,
                const byte* sessionId, word32 sessionIdSz)
{
    word32 blocks, remainder;
    wc_HashAlg hash;
    enum wc_HashType enmhashId = (enum wc_HashType)hashId;
    byte kPad = 0;
    byte pad = 0;
    byte kSzFlat[LENGTH_SZ];
    int digestSz;
    int ret;

    if (key == NULL || keySz == 0 ||
        k == NULL || kSz == 0 ||
        h == NULL || hSz == 0 ||
        sessionId == NULL || sessionIdSz == 0) {

        WLOG(WS_LOG_DEBUG, "GK: bad argument");
        return WS_BAD_ARGUMENT;
    }

    digestSz = wc_HashGetDigestSize(enmhashId);
    if (digestSz <= 0) {
        WLOG(WS_LOG_DEBUG, "GK: bad hash ID");
        return WS_BAD_ARGUMENT;
    }

    if (k[0] & 0x80) kPad = 1;
    c32toa(kSz + kPad, kSzFlat);

    blocks = keySz / digestSz;
    remainder = keySz % digestSz;

    ret = wc_HashInit(&hash, enmhashId);
    if (ret == WS_SUCCESS)
        ret = HashUpdate(&hash, enmhashId, kSzFlat, LENGTH_SZ);
    if (ret == WS_SUCCESS && kPad)
        ret = HashUpdate(&hash, enmhashId, &pad, 1);
    if (ret == WS_SUCCESS)
        ret = HashUpdate(&hash, enmhashId, k, kSz);
    if (ret == WS_SUCCESS)
        ret = HashUpdate(&hash, enmhashId, h, hSz);
    if (ret == WS_SUCCESS)
        ret = HashUpdate(&hash, enmhashId, &keyId, sizeof(keyId));
    if (ret == WS_SUCCESS)
        ret = HashUpdate(&hash, enmhashId, sessionId, sessionIdSz);

    if (ret == WS_SUCCESS) {
        if (blocks == 0) {
            if (remainder > 0) {
                byte lastBlock[WC_MAX_DIGEST_SIZE];
                ret = wc_HashFinal(&hash, enmhashId, lastBlock);
                if (ret == WS_SUCCESS)
                    WMEMCPY(key, lastBlock, remainder);
            }
        }
        else {
            word32 runningKeySz, curBlock;

            runningKeySz = digestSz;
            ret = wc_HashFinal(&hash, enmhashId, key);

            for (curBlock = 1; curBlock < blocks; curBlock++) {
                ret = wc_HashInit(&hash, enmhashId);
                if (ret != WS_SUCCESS) break;
                ret = HashUpdate(&hash, enmhashId, kSzFlat, LENGTH_SZ);
                if (ret != WS_SUCCESS) break;
                if (kPad)
                    ret = HashUpdate(&hash, enmhashId, &pad, 1);
                if (ret != WS_SUCCESS) break;
                ret = HashUpdate(&hash, enmhashId, k, kSz);
                if (ret != WS_SUCCESS) break;
                ret = HashUpdate(&hash, enmhashId, h, hSz);
                if (ret != WS_SUCCESS) break;
                ret = HashUpdate(&hash, enmhashId, key, runningKeySz);
                if (ret != WS_SUCCESS) break;
                ret = wc_HashFinal(&hash, enmhashId, key + runningKeySz);
                if (ret != WS_SUCCESS) break;
                runningKeySz += digestSz;
            }

            if (remainder > 0) {
                byte lastBlock[WC_MAX_DIGEST_SIZE];
                if (ret == WS_SUCCESS)
                    ret = wc_HashInit(&hash, enmhashId);
                if (ret == WS_SUCCESS)
                    ret = HashUpdate(&hash, enmhashId, kSzFlat, LENGTH_SZ);
                if (ret == WS_SUCCESS && kPad)
                    ret = HashUpdate(&hash, enmhashId, &pad, 1);
                if (ret == WS_SUCCESS)
                    ret = HashUpdate(&hash, enmhashId, k, kSz);
                if (ret == WS_SUCCESS)
                    ret = HashUpdate(&hash, enmhashId, h, hSz);
                if (ret == WS_SUCCESS)
                    ret = HashUpdate(&hash, enmhashId, key, runningKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HashFinal(&hash, enmhashId, lastBlock);
                if (ret == WS_SUCCESS)
                    WMEMCPY(key + runningKeySz, lastBlock, remainder);
            }
        }
    }

    if (ret != WS_SUCCESS)
        ret = WS_CRYPTO_FAILED;
    wc_HashFree(&hash, enmhashId);

    return ret;
}


static int GenerateKeys(WOLFSSH* ssh, byte hashId)
{
    Keys* cK = NULL;
    Keys* sK = NULL;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;
    else {
        if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER) {
            cK = &ssh->handshake->peerKeys;
            sK = &ssh->handshake->keys;
        }
        else {
            cK = &ssh->handshake->keys;
            sK = &ssh->handshake->peerKeys;
        }
    }

    if (ret == WS_SUCCESS)
        ret = GenerateKey(hashId, 'A',
                          cK->iv, cK->ivSz,
                          ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                          ssh->sessionId, ssh->sessionIdSz);
    if (ret == WS_SUCCESS)
        ret = GenerateKey(hashId, 'B',
                          sK->iv, sK->ivSz,
                          ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                          ssh->sessionId, ssh->sessionIdSz);
    if (ret == WS_SUCCESS)
        ret = GenerateKey(hashId, 'C',
                          cK->encKey, cK->encKeySz,
                          ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                          ssh->sessionId, ssh->sessionIdSz);
    if (ret == WS_SUCCESS)
        ret = GenerateKey(hashId, 'D',
                          sK->encKey, sK->encKeySz,
                          ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                          ssh->sessionId, ssh->sessionIdSz);
    if (ret == WS_SUCCESS) {
        if (!ssh->handshake->aeadMode) {
            ret = GenerateKey(hashId, 'E',
                              cK->macKey, cK->macKeySz,
                              ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                              ssh->sessionId, ssh->sessionIdSz);
            if (ret == WS_SUCCESS) {
                ret = GenerateKey(hashId, 'F',
                                  sK->macKey, sK->macKeySz,
                                  ssh->k, ssh->kSz, ssh->h, ssh->hSz,
                                  ssh->sessionId, ssh->sessionIdSz);
            }
        }
    }
#ifdef SHOW_SECRETS
    if (ret == WS_SUCCESS) {
        printf("\n** Showing Secrets **\nK:\n");
        DumpOctetString(ssh->k, ssh->kSz);
        printf("H:\n");
        DumpOctetString(ssh->h, ssh->hSz);
        printf("Session ID:\n");
        DumpOctetString(ssh->sessionId, ssh->sessionIdSz);
        printf("A:\n");
        DumpOctetString(cK->iv, cK->ivSz);
        printf("B:\n");
        DumpOctetString(sK->iv, sK->ivSz);
        printf("C:\n");
        DumpOctetString(cK->encKey, cK->encKeySz);
        printf("D:\n");
        DumpOctetString(sK->encKey, sK->encKeySz);
        printf("E:\n");
        DumpOctetString(cK->macKey, cK->macKeySz);
        printf("F:\n");
        DumpOctetString(sK->macKey, sK->macKeySz);
        printf("\n");
    }
#endif /* SHOW_SECRETS */

    return ret;
}


typedef struct {
    byte id;
    const char* name;
} NameIdPair;


static const NameIdPair NameIdMap[] = {
    { ID_NONE, "none" },

    /* Encryption IDs */
#ifndef WOLFSSH_NO_AES_CBC
    { ID_AES128_CBC, "aes128-cbc" },
    { ID_AES192_CBC, "aes192-cbc" },
    { ID_AES256_CBC, "aes256-cbc" },
#endif
#ifndef WOLFSSH_NO_AES_CTR
    { ID_AES128_CTR, "aes128-ctr" },
    { ID_AES192_CTR, "aes192-ctr" },
    { ID_AES256_CTR, "aes256-ctr" },
#endif
#ifndef WOLFSSH_NO_AES_GCM
    { ID_AES128_GCM, "aes128-gcm@openssh.com" },
    { ID_AES192_GCM, "aes192-gcm@openssh.com" },
    { ID_AES256_GCM, "aes256-gcm@openssh.com" },
#endif

    /* Integrity IDs */
#ifndef WOLFSSH_NO_HMAC_SHA1
    { ID_HMAC_SHA1, "hmac-sha1" },
#endif
#ifndef WOLFSSH_NO_HMAC_SHA1_96
    { ID_HMAC_SHA1_96, "hmac-sha1-96" },
#endif
#ifndef WOLFSSH_NO_HMAC_SHA2_256
    { ID_HMAC_SHA2_256, "hmac-sha2-256" },
#endif

    /* Key Exchange IDs */
#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
    { ID_DH_GROUP1_SHA1, "diffie-hellman-group1-sha1" },
#endif
#ifndef WOLFSSH_NO_DH_GROUP14_SHA1
    { ID_DH_GROUP14_SHA1, "diffie-hellman-group14-sha1" },
#endif
#ifndef WOLFSSH_NO_DH_GEX_SHA256
    { ID_DH_GEX_SHA256, "diffie-hellman-group-exchange-sha256" },
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
    { ID_ECDH_SHA2_NISTP256, "ecdh-sha2-nistp256" },
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
    { ID_ECDH_SHA2_NISTP384, "ecdh-sha2-nistp384" },
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
    { ID_ECDH_SHA2_NISTP521, "ecdh-sha2-nistp521" },
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_ED25519
    { ID_ECDH_SHA2_ED25519, "curve25519-sha256" },
    { ID_ECDH_SHA2_ED25519_LIBSSH, "curve25519-sha256@libssh.org" },
#endif
#ifndef WOLFSSH_NO_DH_GEX_SHA256
    { ID_DH_GROUP14_SHA256, "diffie-hellman-group14-sha256" },
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
    /* We use kyber-512 here to achieve interop with OQS's fork. */
    { ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256, "ecdh-sha2-nistp256-kyber-512-sha256" },
#endif
    /* Public Key IDs */
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    { ID_SSH_RSA, "ssh-rsa" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    { ID_ECDSA_SHA2_NISTP256, "ecdsa-sha2-nistp256" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    { ID_ECDSA_SHA2_NISTP384, "ecdsa-sha2-nistp384" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    { ID_ECDSA_SHA2_NISTP521, "ecdsa-sha2-nistp521" },
#endif
#ifdef WOLFSSH_CERTS
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    { ID_X509V3_SSH_RSA, "x509v3-ssh-rsa" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    { ID_X509V3_ECDSA_SHA2_NISTP256, "x509v3-ecdsa-sha2-nistp256" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    { ID_X509V3_ECDSA_SHA2_NISTP384, "x509v3-ecdsa-sha2-nistp384" },
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    { ID_X509V3_ECDSA_SHA2_NISTP521, "x509v3-ecdsa-sha2-nistp521" },
#endif
#endif /* WOLFSSH_CERTS */

    /* Service IDs */
    { ID_SERVICE_USERAUTH, "ssh-userauth" },
    { ID_SERVICE_CONNECTION, "ssh-connection" },

    /* UserAuth IDs */
    { ID_USERAUTH_PASSWORD, "password" },
    { ID_USERAUTH_PUBLICKEY, "publickey" },

    /* Channel Type IDs */
    { ID_CHANTYPE_SESSION, "session" },
#ifdef WOLFSSH_FWD
    { ID_CHANTYPE_TCPIP_FORWARD, "forwarded-tcpip" },
    { ID_CHANTYPE_TCPIP_DIRECT, "direct-tcpip" },
#endif /* WOLFSSH_FWD */
#ifdef WOLFSSH_AGENT
    { ID_CHANTYPE_AUTH_AGENT, "auth-agent@openssh.com" },
#endif /* WOLFSSH_AGENT */

    /* Global Request IDs */
#ifdef WOLFSSH_FWD
    { ID_GLOBREQ_TCPIP_FWD, "tcpip-forward" },
    { ID_GLOBREQ_TCPIP_FWD_CANCEL, "cancel-tcpip-forward" },
#endif /* WOLFSSH_FWD */
};


byte NameToId(const char* name, word32 nameSz)
{
    byte id = ID_UNKNOWN;
    word32 i;

    for (i = 0; i < (sizeof(NameIdMap)/sizeof(NameIdPair)); i++) {
        if (nameSz == (word32)WSTRLEN(NameIdMap[i].name) &&
            XMEMCMP(name, NameIdMap[i].name, nameSz) == 0) {

            id = NameIdMap[i].id;
            break;
        }
    }

    return id;
}


const char* IdToName(byte id)
{
    const char* name = "unknown";
    word32 i;

    for (i = 0; i < (sizeof(NameIdMap)/sizeof(NameIdPair)); i++) {
        if (NameIdMap[i].id == id) {
            name = NameIdMap[i].name;
            break;
        }
    }

    return name;
}


WOLFSSH_CHANNEL* ChannelNew(WOLFSSH* ssh, byte channelType,
                            word32 initialWindowSz, word32 maxPacketSz)
{
    WOLFSSH_CHANNEL* newChannel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering ChannelNew()");
    if (ssh == NULL || ssh->ctx == NULL) {
        WLOG(WS_LOG_DEBUG, "Trying to create new channel without ssh or ctx");
    }
    else {
        void* heap = ssh->ctx->heap;

        newChannel = (WOLFSSH_CHANNEL*)WMALLOC(sizeof(WOLFSSH_CHANNEL),
                                               heap, DYNTYPE_CHANNEL);
        if (newChannel != NULL)
        {
            byte* buffer;

            buffer = (byte*)WMALLOC(initialWindowSz, heap, DYNTYPE_BUFFER);
            if (buffer != NULL) {
                WMEMSET(newChannel, 0, sizeof(WOLFSSH_CHANNEL));
                newChannel->ssh = ssh;
                newChannel->channelType = channelType;
                newChannel->channel = ssh->nextChannel++;
                WLOG(WS_LOG_DEBUG, "New channel id = %u", newChannel->channel);
                newChannel->windowSz = initialWindowSz;
                newChannel->maxPacketSz = maxPacketSz;
                /*
                 * In the context of the channel input buffer, the buffer is
                 * a fixed size. The property length will be the insert point
                 * for new received data. The property idx will be the pull
                 * point for the data.
                 */
                newChannel->inputBuffer.heap = heap;
                newChannel->inputBuffer.buffer = buffer;
                newChannel->inputBuffer.bufferSz = initialWindowSz;
                newChannel->inputBuffer.dynamicFlag = 1;
            }
            else {
                WLOG(WS_LOG_DEBUG, "Unable to allocate new channel's buffer");
                WFREE(newChannel, heap, DYNTYPE_CHANNEL);
                newChannel = NULL;
            }
        }
        else {
            WLOG(WS_LOG_DEBUG, "Unable to allocate new channel");
        }
    }

    WLOG(WS_LOG_INFO, "Leaving ChannelNew(), ret = %p", newChannel);

    return newChannel;
}


void ChannelDelete(WOLFSSH_CHANNEL* channel, void* heap)
{
    WOLFSSH_UNUSED(heap);

    if (channel) {
    #ifdef WOLFSSH_FWD
        if (channel->host)
            WFREE(channel->host, heap, DYNTYPE_STRING);
        if (channel->origin)
            WFREE(channel->origin, heap, DYNTYPE_STRING);
    #endif /* WOLFSSH_FWD */
        WFREE(channel->inputBuffer.buffer,
              channel->inputBuffer.heap, DYNTYPE_BUFFER);
        if (channel->command)
            WFREE(channel->command, heap, DYNTYPE_STRING);
        WFREE(channel, heap, DYNTYPE_CHANNEL);
    }
}


WOLFSSH_CHANNEL* ChannelFind(WOLFSSH* ssh, word32 channel, byte peer)
{
    WOLFSSH_CHANNEL* findChannel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering ChannelFind(): %s %u",
         peer ? "peer" : "self", channel);

    if (ssh == NULL) {
        WLOG(WS_LOG_DEBUG, "Null ssh, not looking for channel");
    }
    else {
        WOLFSSH_CHANNEL* list = ssh->channelList;
        word32 listSz = ssh->channelListSz;

        while (list && listSz) {
            if (channel == ((peer == WS_CHANNEL_ID_PEER) ?
                            list->peerChannel : list->channel)) {
                findChannel = list;
                break;
            }
            list = list->next;
            listSz--;
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving ChannelFind(): %p", findChannel);

    return findChannel;
}


int ChannelUpdatePeer(WOLFSSH_CHANNEL* channel, word32 peerChannelId,
                  word32 peerInitialWindowSz, word32 peerMaxPacketSz)
{
    int ret = WS_SUCCESS;

    if (channel == NULL)
        ret = WS_BAD_ARGUMENT;
    else {
        channel->peerChannel = peerChannelId;
        channel->peerWindowSz = peerInitialWindowSz;
        channel->peerMaxPacketSz = peerMaxPacketSz;
        channel->openConfirmed = 1;
    }

    return ret;
}


#ifdef WOLFSSH_FWD
int ChannelUpdateForward(WOLFSSH_CHANNEL* channel,
                                const char* host, word32 hostPort,
                                const char* origin, word32 originPort,
                                int isDirect)
{
    int ret = WS_SUCCESS;
    char* hostCopy = NULL;
    char* originCopy = NULL;
    word32 hostSz;
    word32 originSz;

    if (channel == NULL || host == NULL || origin == NULL)
        ret = WS_BAD_ARGUMENT;
    else {
        void* heap = channel->ssh->ctx->heap;

        hostSz = (word32)WSTRLEN(host) + 1;
        originSz = (word32)WSTRLEN(origin) + 1;
        hostCopy = (char*)WMALLOC(hostSz, heap, DYNTYPE_STRING);
        originCopy = (char*)WMALLOC(originSz, heap, DYNTYPE_STRING);
        if (hostCopy == NULL || originCopy == NULL) {
            WFREE(hostCopy, heap, DYNTYPE_STRING);
            WFREE(originCopy, heap, DYNTYPE_STRING);
            ret = WS_MEMORY_E;
        }
    }

    if (ret == WS_SUCCESS) {
        WSTRNCPY(hostCopy, host, hostSz);
        WSTRNCPY(originCopy, origin, originSz);

        /* delete any existing host and origin in the channel */
        if (channel->host)
            WFREE(channel->host, heap, DYNTYPE_STRING);
        if (channel->origin)
            WFREE(channel->origin, heap, DYNTYPE_STRING);

        channel->host = hostCopy;
        channel->hostPort = hostPort;
        channel->origin = originCopy;
        channel->originPort = originPort;
        channel->isDirect = isDirect;
    }

    return ret;
}
#endif /* WOLFSSH_FWD */


int ChannelAppend(WOLFSSH* ssh, WOLFSSH_CHANNEL* channel)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering ChannelAppend()");

    if (ssh == NULL || channel == NULL) {
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving ChannelAppend(), ret = %d", ret);
        return ret;
    }

    if (ssh->channelList == NULL) {
        ssh->channelList = channel;
        ssh->channelListSz = 1;
    }
    else {
        WOLFSSH_CHANNEL* cur = ssh->channelList;
        while (cur->next != NULL)
            cur = cur->next;
        cur->next = channel;
        ssh->channelListSz++;
    }

    WLOG(WS_LOG_DEBUG, "Leaving ChannelAppend(), ret = %d", ret);
    return ret;
}


int ChannelRemove(WOLFSSH* ssh, word32 channel, byte peer)
{
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* list;

    WLOG(WS_LOG_DEBUG, "Entering ChannelRemove()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        list = ssh->channelList;
        if (list == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        WOLFSSH_CHANNEL* prev = NULL;
        word32 listSz = ssh->channelListSz;

        while (list && listSz) {
            if (channel == ((peer == WS_CHANNEL_ID_PEER) ?
                            list->peerChannel : list->channel)) {
                if (prev == NULL)
                    ssh->channelList = list->next;
                else
                    prev->next = list->next;
                ChannelDelete(list, ssh->ctx->heap);
                ssh->channelListSz--;

                break;
            }
            prev = list;
            list = list->next;
            listSz--;
        }

        if (listSz == 0)
            ret = WS_INVALID_CHANID;
    }

    WLOG(WS_LOG_DEBUG, "Leaving ChannelRemove(), ret = %d", ret);
    return ret;
}


int ChannelPutData(WOLFSSH_CHANNEL* channel, byte* data, word32 dataSz)
{
    WOLFSSH_BUFFER* inBuf;

    WLOG(WS_LOG_DEBUG, "Entering ChannelPutData()");

    if (channel == NULL || data == NULL)
        return WS_BAD_ARGUMENT;

    inBuf = &channel->inputBuffer;

    /* sanity check the current state to see if is too much data */
    if (dataSz > channel->windowSz) {
        WLOG(WS_LOG_ERROR, "Internal state error, too much data");
        return WS_FATAL_ERROR;
    }

    if (inBuf->length < inBuf->bufferSz &&
        inBuf->length + dataSz <= inBuf->bufferSz) {

        WMEMCPY(inBuf->buffer + inBuf->length, data, dataSz);
        inBuf->length += dataSz;

        WLOG(WS_LOG_INFO, "  dataSz = %u", dataSz);
        WLOG(WS_LOG_INFO, "  windowSz = %u", channel->windowSz);
        channel->windowSz -= dataSz;
        WLOG(WS_LOG_INFO, "  update windowSz = %u", channel->windowSz);
    }
    else {
        return WS_RECV_OVERFLOW_E;
    }

    return WS_SUCCESS;
}


int BufferInit(WOLFSSH_BUFFER* buffer, word32 size, void* heap)
{
    if (buffer == NULL)
        return WS_BAD_ARGUMENT;

    if (size <= STATIC_BUFFER_LEN)
        size = STATIC_BUFFER_LEN;

    WMEMSET(buffer, 0, sizeof(WOLFSSH_BUFFER));
    buffer->heap = heap;
    buffer->bufferSz = size;
    if (size > STATIC_BUFFER_LEN) {
        buffer->buffer = (byte*)WMALLOC(size, heap, DYNTYPE_BUFFER);
        if (buffer->buffer == NULL)
            return WS_MEMORY_E;
        buffer->dynamicFlag = 1;
    }
    else
        buffer->buffer = buffer->staticBuffer;

    return WS_SUCCESS;
}


int GrowBuffer(WOLFSSH_BUFFER* buf, word32 sz, word32 usedSz)
{
#if 0
    WLOG(WS_LOG_DEBUG, "GB: buf = %p", buf);
    WLOG(WS_LOG_DEBUG, "GB: sz = %d", sz);
    WLOG(WS_LOG_DEBUG, "GB: usedSz = %d", usedSz);
#endif
    /* New buffer will end up being sz+usedSz long
     * empty space at the head of the buffer will be compressed */
    if (buf != NULL) {
        word32 newSz = sz + usedSz;
        /*WLOG(WS_LOG_DEBUG, "GB: newSz = %d", newSz);*/

        if (newSz > buf->bufferSz) {
            byte* newBuffer = (byte*)WMALLOC(newSz,
                                                     buf->heap, DYNTYPE_BUFFER);

            if (newBuffer == NULL) {
                WLOG(WS_LOG_ERROR, "Not enough memory left to grow buffer");
                return WS_MEMORY_E;
            }

            /*WLOG(WS_LOG_DEBUG, "GB: resizing buffer");*/
            if (buf->length > 0 && usedSz > 0)
                WMEMCPY(newBuffer, buf->buffer + buf->idx, usedSz);

            if (!buf->dynamicFlag)
                buf->dynamicFlag = 1;
            else
                WFREE(buf->buffer, buf->heap, DYNTYPE_BUFFER);

            buf->buffer = newBuffer;
            buf->bufferSz = newSz;
            buf->length = usedSz;
            buf->idx = 0;
        }
    }

    return WS_SUCCESS;
}


void ShrinkBuffer(WOLFSSH_BUFFER* buf, int forcedFree)
{
    WLOG(WS_LOG_DEBUG, "Entering ShrinkBuffer()");

    if (buf != NULL) {
        word32 usedSz = buf->length - buf->idx;

        WLOG(WS_LOG_DEBUG, "SB: usedSz = %u, forcedFree = %u",
             usedSz, forcedFree);

        if (!forcedFree && usedSz > STATIC_BUFFER_LEN)
            return;

        if (!forcedFree && usedSz) {
            WLOG(WS_LOG_DEBUG, "SB: shifting down");
            WMEMCPY(buf->staticBuffer, buf->buffer + buf->idx, usedSz);
        }

        if (buf->dynamicFlag) {
            WLOG(WS_LOG_DEBUG, "SB: releasing dynamic buffer");
            WFREE(buf->buffer, buf->heap, DYNTYPE_BUFFER);
        }
        buf->dynamicFlag = 0;
        buf->buffer = buf->staticBuffer;
        buf->bufferSz = STATIC_BUFFER_LEN;
        buf->length = forcedFree ? 0 : usedSz;
        buf->idx = 0;
    }

    WLOG(WS_LOG_DEBUG, "Leaving ShrinkBuffer()");
}


static int ReceiveData(WOLFSSH* ssh, byte* buf, word32 sz)
{
    int recvd;

    if (ssh->ctx->ioRecvCb == NULL) {
        WLOG(WS_LOG_DEBUG, "Your IO Recv callback is null, please set");
        return -1;
    }

retry:
    recvd = ssh->ctx->ioRecvCb(ssh, buf, sz, ssh->ioReadCtx);
    WLOG(WS_LOG_DEBUG, "Receive: recvd = %d", recvd);
    if (recvd < 0)
        switch (recvd) {
            case WS_CBIO_ERR_GENERAL:        /* general/unknown error */
                return -1;

            case WS_CBIO_ERR_WANT_READ:      /* want read, would block */
                return WS_WANT_READ;

            case WS_CBIO_ERR_CONN_RST:       /* connection reset */
                ssh->connReset = 1;
                return -1;

            case WS_CBIO_ERR_ISR:            /* interrupt */
                goto retry;

            case WS_CBIO_ERR_CONN_CLOSE:     /* peer closed connection */
                ssh->isClosed = 1;
                return -1;

            case WS_CBIO_ERR_TIMEOUT:
                return -1;

            default:
                return recvd;
        }

    return recvd;
}


static int GetInputText(WOLFSSH* ssh, byte** pEol)
{
    int gotLine = 0;
    int inSz = 255;
    int in;
    char *eol;

    if (GrowBuffer(&ssh->inputBuffer, inSz, 0) < 0)
        return WS_MEMORY_E;

    do {
        in = ReceiveData(ssh,
                     ssh->inputBuffer.buffer + ssh->inputBuffer.length, inSz);

        if (in == -1)
            return WS_SOCKET_ERROR_E;

        if (in == WS_WANT_READ)
            return WS_WANT_READ;

        if (in > inSz)
            return WS_RECV_OVERFLOW_E;

        ssh->inputBuffer.length += in;
        inSz -= in;

        eol = WSTRNSTR((const char*)ssh->inputBuffer.buffer, "\r\n",
                       ssh->inputBuffer.length);

        /* section 4.2 in RFC 4253 states that can be lenient on the CR for
         * interop with older or undocumented versions of SSH */
        if (!eol) {
            WLOG(WS_LOG_DEBUG, "Checking for old version of protocol exchange");
            eol = WSTRNSTR((const char*)ssh->inputBuffer.buffer, "\n",
                       ssh->inputBuffer.length);
        }

        if (eol)
            gotLine = 1;

    } while (!gotLine && inSz);

    if (pEol)
        *pEol = (byte*)eol;

    return (gotLine ? WS_SUCCESS : WS_VERSION_E);
}


/* returns WS_SUCCESS on success */
int wolfSSH_SendPacket(WOLFSSH* ssh)
{
    WLOG(WS_LOG_DEBUG, "Entering wolfSSH_SendPacket()");

    if (ssh->ctx->ioSendCb == NULL) {
        WLOG(WS_LOG_DEBUG, "Your IO Send callback is null, please set");
        return WS_SOCKET_ERROR_E;
    }

    while (ssh->outputBuffer.length > 0) {
        int sent;

        /* sanity check on amount requested to be sent */
        if (ssh->outputBuffer.idx + ssh->outputBuffer.length >
                ssh->outputBuffer.bufferSz) {
            WLOG(WS_LOG_ERROR, "Bad buffer state");
            return WS_BUFFER_E;
        }

       sent = ssh->ctx->ioSendCb(ssh,
                               ssh->outputBuffer.buffer + ssh->outputBuffer.idx,
                               ssh->outputBuffer.length, ssh->ioWriteCtx);

        if (sent < 0) {
            switch (sent) {
                case WS_CBIO_ERR_WANT_WRITE:     /* want write, would block */
                    ssh->error = WS_WANT_WRITE;
                    return WS_WANT_WRITE;

                case WS_CBIO_ERR_CONN_RST:       /* connection reset */
                    ssh->connReset = 1;
                    break;

                case WS_CBIO_ERR_CONN_CLOSE:     /* peer closed connection */
                    ssh->isClosed = 1;
                    break;

                case WS_CBIO_ERR_GENERAL:
                    ShrinkBuffer(&ssh->outputBuffer, 1);
            }
            return WS_SOCKET_ERROR_E;
        }

        if ((word32)sent > ssh->outputBuffer.length) {
            WLOG(WS_LOG_DEBUG, "wolfSSH_SendPacket() out of bounds read");
            return WS_SEND_OOB_READ_E;
        }

        ssh->outputBuffer.idx += sent;
        ssh->outputBuffer.length -= sent;
    }

    ssh->outputBuffer.idx = 0;
    ssh->outputBuffer.plainSz = 0;

    WLOG(WS_LOG_DEBUG, "SB: Shrinking output buffer");
    ShrinkBuffer(&ssh->outputBuffer, 0);
    return HighwaterCheck(ssh, WOLFSSH_HWSIDE_TRANSMIT);
}


static int GetInputData(WOLFSSH* ssh, word32 size)
{
    int in;
    int inSz;
    int maxLength;
    int usedLength;

    /* check max input length */
    usedLength = ssh->inputBuffer.length - ssh->inputBuffer.idx;
    maxLength  = ssh->inputBuffer.bufferSz - usedLength;
    inSz       = (int)(size - usedLength);      /* from last partial read */
#if 0
    WLOG(WS_LOG_DEBUG, "GID: size = %u", size);
    WLOG(WS_LOG_DEBUG, "GID: usedLength = %d", usedLength);
    WLOG(WS_LOG_DEBUG, "GID: maxLength = %d", maxLength);
    WLOG(WS_LOG_DEBUG, "GID: inSz = %d", inSz);
#endif
    /*
     * usedLength - how much untouched data is in the buffer
     * maxLength - how much empty space is in the buffer
     * inSz - difference between requested data and empty space in the buffer
     *        how much more we need to allocate
     */

    if (inSz <= 0)
        return WS_SUCCESS;

    /*
     * If we need more space than there is left in the buffer grow buffer.
     * Growing the buffer also compresses empty space at the head of the
     * buffer and resets idx to 0.
     */
    if (inSz > maxLength) {
        if (GrowBuffer(&ssh->inputBuffer, size, usedLength) < 0) {
            ssh->error = WS_MEMORY_E;
            return WS_FATAL_ERROR;
        }
    }

    /* Put buffer data at start if not there */
    /* Compress the buffer if needed, i.e. buffer idx is non-zero */
    if (usedLength > 0 && ssh->inputBuffer.idx != 0) {
        WMEMMOVE(ssh->inputBuffer.buffer,
                ssh->inputBuffer.buffer + ssh->inputBuffer.idx,
                usedLength);
    }

    /* remove processed data */
    ssh->inputBuffer.idx    = 0;
    ssh->inputBuffer.length = usedLength;

    /* read data from network */
    do {
        in = ReceiveData(ssh,
                     ssh->inputBuffer.buffer + ssh->inputBuffer.length, inSz);
        if (in == -1) {
            ssh->error = WS_SOCKET_ERROR_E;
            return WS_FATAL_ERROR;
        }

        if (in == WS_WANT_READ) {
            ssh->error = WS_WANT_READ;
            return WS_FATAL_ERROR;
        }

        if (in > inSz) {
            ssh->error = WS_RECV_OVERFLOW_E;
            return WS_FATAL_ERROR;
        }

        if (in >= 0) {
            ssh->inputBuffer.length += in;
            inSz -= in;
        }
        else {
            /* all other unexpected negative values is a failure case */
            ssh->error = WS_FATAL_ERROR;
            return WS_FATAL_ERROR;
        }

    } while (ssh->inputBuffer.length < size);

    return WS_SUCCESS;
}


int GetBoolean(byte* v, const byte* buf, word32 len, word32* idx)
{
    int result = WS_BUFFER_E;

    if (*idx < len) {
        *v = buf[*idx];
        *idx += BOOLEAN_SZ;
        result = WS_SUCCESS;
    }

    return result;
}


int GetUint32(word32* v, const byte* buf, word32 len, word32* idx)
{
    int result = WS_BUFFER_E;

    if (*idx < len && UINT32_SZ <= len - *idx) {
        ato32(buf + *idx, v);
        *idx += UINT32_SZ;
        result = WS_SUCCESS;
    }

    return result;
}


int GetSize(word32* v, const byte* buf, word32 len, word32* idx)
{
    int result;

    result = GetUint32(v, buf, len, idx);
    if (result == WS_SUCCESS) {
        if (*v > len - *idx) {
            result = WS_BUFFER_E;
        }
    }

    return result;
}


/* Gets the size of the mpint, and puts the pointer to the start of
 * buf's number into *mpint. This function does not copy. */
int GetMpint(word32* mpintSz, const byte** mpint,
        const byte* buf, word32 len, word32* idx)
{
    int result;

    result = GetUint32(mpintSz, buf, len, idx);

    if (result == WS_SUCCESS) {
        result = WS_BUFFER_E;

        if (*idx < len && *mpintSz <= len - *idx) {
            *mpint = buf + *idx;
            *idx += *mpintSz;
            result = WS_SUCCESS;
        }
    }

    return result;
}


/* Gets the size of a string, copies it as much of it as will fit in
 * the provided buffer, and terminates it with a NULL. */
int GetString(char* s, word32* sSz, const byte* buf, word32 len, word32 *idx)
{
    int result;
    word32 strSz;

    result = GetUint32(&strSz, buf, len, idx);

    if (result == WS_SUCCESS) {
        result = WS_BUFFER_E;

        /* This allows 0 length string to be decoded */
        if (*idx <= len && strSz <= len - *idx) {
            *sSz = (strSz >= *sSz) ? *sSz - 1 : strSz; /* -1 for null char */
            WMEMCPY(s, buf + *idx, *sSz);
            *idx += strSz;
            s[*sSz] = 0;
            result = WS_SUCCESS;
        }
    }

    return result;
}


/* Gets the size of a string, allocates memory to hold it plus a NULL, then
 * copies it into the allocated buffer, and terminates it with a NULL. */
int GetStringAlloc(void* heap, char** s, const byte* buf, word32 len, word32 *idx)
{
    int result;
    char* str;
    word32 strSz;

    result = GetUint32(&strSz, buf, len, idx);

    if (result == WS_SUCCESS) {
        if (*idx >= len || strSz > len - *idx)
            return WS_BUFFER_E;
        str = (char*)WMALLOC(strSz + 1, heap, DYNTYPE_STRING);
        if (str == NULL)
            return WS_MEMORY_E;
        WMEMCPY(str, buf + *idx, strSz);
        *idx += strSz;
        str[strSz] = '\0';

        if (*s != NULL)
            WFREE(*s, heap, DYNTYPE_STRING);
        *s = str;
    }

    return result;
}


/* Gets the size of the string, and puts the pointer to the start of
 * buf's string into *str. This function does not copy. */
int GetStringRef(word32* strSz, const byte** str,
        const byte* buf, word32 len, word32* idx)
{
    int result;

    result = GetUint32(strSz, buf, len, idx);

    if (result == WS_SUCCESS) {
        result = WS_BUFFER_E;

        if (*idx < len && *strSz <= len - *idx) {
            *str = buf + *idx;
            *idx += *strSz;
            result = WS_SUCCESS;
        }
    }

    return result;
}


static int GetNameList(byte* idList, word32* idListSz,
                       byte* buf, word32 len, word32* idx)
{
    byte idListIdx;
    word32 nameListSz, nameListIdx;
    word32 begin;
    byte* name;
    word32 nameSz;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering GetNameList()");

    if (idList == NULL || idListSz == NULL ||
        buf == NULL || len == 0 || idx == NULL) {

        ret = WS_BAD_ARGUMENT;
    }

    /*
     * This iterates across a name list and finds names that end in either the
     * comma delimeter or with the end of the list.
     */

    if (ret == WS_SUCCESS) {
        begin = *idx;
        if (begin >= len || begin + 4 >= len)
            ret = WS_BUFFER_E;
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(&nameListSz, buf, len, &begin);

    /* The strings we want are now in the bounds of the message, and the
     * length of the list. Find the commas, or end of list, and then decode
     * the values. */
    if (ret == WS_SUCCESS) {
        name = buf + begin;
        nameSz = 0;
        nameListIdx = 0;
        idListIdx = 0;

        while (nameListIdx < nameListSz) {
            nameListIdx++;

            if (nameListIdx == nameListSz)
                nameSz++;

            if (nameListIdx + begin >= len)
                return WS_BUFFER_E;

            if (nameListIdx == nameListSz || name[nameSz] == ',') {
                byte id;

                id = NameToId((char*)name, nameSz);
                {
                    const char* displayName = IdToName(id);
                    if (displayName) {
                        WLOG(WS_LOG_DEBUG, "DNL: name ID = %s", displayName);
                    }
                }
                if (id != ID_UNKNOWN || idListIdx == 0) {
                    /* Intentionally save the first one if unknown. This helps
                     * skipping the KexDhInit if the client sends the wrong one
                     * as a guess. */
                    if (idListIdx >= *idListSz) {
                        WLOG(WS_LOG_ERROR, "No more space left for names");
                        return WS_BUFFER_E;
                    }
                    idList[idListIdx++] = id;
                }

                name += 1 + nameSz;
                nameSz = 0;
            }
            else
                nameSz++;
        }

        begin += nameListSz;
        *idListSz = idListIdx;
        *idx = begin;
    }

    WLOG(WS_LOG_DEBUG, "Leaving GetNameList(), ret = %d", ret);
    return ret;
}

static const byte cannedEncAlgo[] = {
#ifndef WOLFSSH_NO_AES_GCM
    ID_AES256_GCM,
    ID_AES192_GCM,
    ID_AES128_GCM,
#endif
#ifndef WOLFSSH_NO_AES_CTR
    ID_AES256_CTR,
    ID_AES192_CTR,
    ID_AES128_CTR,
#endif
#ifndef WOLFSSH_NO_AES_CBC
    ID_AES256_CBC,
    ID_AES192_CBC,
    ID_AES128_CBC,
#endif
};

static const byte cannedMacAlgo[] = {
#ifndef WOLFSSH_NO_HMAC_SHA2_256
    ID_HMAC_SHA2_256,
#endif
#ifndef WOLFSSH_NO_HMAC_SHA1_96
    ID_HMAC_SHA1_96,
#endif
#ifndef WOLFSSH_NO_HMAC_SHA1
    ID_HMAC_SHA1,
#endif
};

static const byte  cannedKeyAlgoClient[] = {
#ifdef WOLFSSH_CERTS
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    ID_X509V3_ECDSA_SHA2_NISTP521,
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    ID_X509V3_ECDSA_SHA2_NISTP384,
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    ID_X509V3_ECDSA_SHA2_NISTP256,
#endif
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    ID_X509V3_SSH_RSA,
#endif
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    ID_ECDSA_SHA2_NISTP521,
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    ID_ECDSA_SHA2_NISTP384,
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    ID_ECDSA_SHA2_NISTP256,
#endif
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    ID_SSH_RSA,
#endif
};

static const byte cannedKexAlgo[] = {
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
    ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256,
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
    ID_ECDH_SHA2_NISTP521,
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
    ID_ECDH_SHA2_NISTP384,
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
    ID_ECDH_SHA2_NISTP256,
#endif
#ifndef WOLFSSH_NO_DH_GEX_SHA256
    ID_DH_GEX_SHA256,
#endif
#ifndef WOLFSSH_NO_DH_GROUP14_SHA1
    ID_DH_GROUP14_SHA1,
#endif
#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
    ID_DH_GROUP1_SHA1,
#endif
};

static const word32 cannedEncAlgoSz = sizeof(cannedEncAlgo);
static const word32 cannedMacAlgoSz = sizeof(cannedMacAlgo);
static const word32 cannedKeyAlgoClientSz = sizeof(cannedKeyAlgoClient);
static const word32 cannedKexAlgoSz = sizeof(cannedKexAlgo);


static byte MatchIdLists(int side, const byte* left, word32 leftSz,
                         const byte* right, word32 rightSz)
{
    word32 i, j;

    /* When matching on the client, swap left and right. Left should be
     * the client's list and right should be the server's list. */
    if (side == WOLFSSH_ENDPOINT_CLIENT) {
        const byte* swap = left;
        word32 swapSz = leftSz;

        left = right;
        right = swap;
        leftSz = rightSz;
        rightSz = swapSz;
    }

    if (left != NULL && leftSz > 0 && right != NULL && rightSz > 0) {
        for (i = 0; i < leftSz; i++) {
            for (j = 0; j < rightSz; j++) {
                if (left[i] == right[j]) {
#if 0
                    WLOG(WS_LOG_DEBUG, "MID: matched %s", IdToName(left[i]));
#endif
                    return left[i];
                }
            }
        }
    }

    return ID_UNKNOWN;
}


static INLINE byte BlockSzForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_AES_CBC
        case ID_AES128_CBC:
        case ID_AES192_CBC:
        case ID_AES256_CBC:
            return AES_BLOCK_SIZE;
#endif
#ifndef WOLFSSH_NO_AES_CTR
        case ID_AES128_CTR:
        case ID_AES192_CTR:
        case ID_AES256_CTR:
            return AES_BLOCK_SIZE;
#endif
#ifndef WOLFSSH_NO_AES_GCM
        case ID_AES128_GCM:
        case ID_AES192_GCM:
        case ID_AES256_GCM:
            return AES_BLOCK_SIZE;
#endif
        default:
            return 0;
    }
}


static INLINE byte MacSzForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_HMAC_SHA1
        case ID_HMAC_SHA1:
            return WC_SHA_DIGEST_SIZE;
#endif
#ifndef WOLFSSH_NO_HMAC_SHA1_96
        case ID_HMAC_SHA1_96:
            return SHA1_96_SZ;
#endif
#ifndef WOLFSSH_NO_HMAC_SHA2_256
        case ID_HMAC_SHA2_256:
            return WC_SHA256_DIGEST_SIZE;
#endif
        default:
            return 0;
    }
}


static INLINE byte KeySzForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_HMAC_SHA1
        case ID_HMAC_SHA1:
            return WC_SHA_DIGEST_SIZE;
#endif
#ifndef WOLFSSH_NO_HMAC_SHA1_96
        case ID_HMAC_SHA1_96:
            return WC_SHA_DIGEST_SIZE;
#endif
#ifndef WOLFSSH_NO_HMAC_SHA2_256
        case ID_HMAC_SHA2_256:
            return WC_SHA256_DIGEST_SIZE;
#endif
#ifndef WOLFSSH_NO_AES_CBC
        case ID_AES128_CBC:
            return AES_128_KEY_SIZE;
        case ID_AES192_CBC:
            return AES_192_KEY_SIZE;
        case ID_AES256_CBC:
            return AES_256_KEY_SIZE;
#endif
#ifndef WOLFSSH_NO_AES_CTR
        case ID_AES128_CTR:
            return AES_128_KEY_SIZE;
        case ID_AES192_CTR:
            return AES_192_KEY_SIZE;
        case ID_AES256_CTR:
            return AES_256_KEY_SIZE;
#endif
#ifndef WOLFSSH_NO_AES_GCM
        case ID_AES128_GCM:
            return AES_128_KEY_SIZE;
        case ID_AES192_GCM:
            return AES_192_KEY_SIZE;
        case ID_AES256_GCM:
            return AES_256_KEY_SIZE;
#endif
        default:
            return 0;
    }
}


static INLINE enum wc_HashType HashForId(byte id)
{
    switch (id) {

        /* SHA1 */
#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
        case ID_DH_GROUP1_SHA1:
            return WC_HASH_TYPE_SHA;
#endif
#ifndef WOLFSSH_NO_DH_GROUP14_SHA1
        case ID_DH_GROUP14_SHA1:
            return WC_HASH_TYPE_SHA;
#endif
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
        case ID_SSH_RSA:
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_SSH_RSA:
    #endif
            return WC_HASH_TYPE_SHA;
#endif

        /* SHA2-256 */
#ifndef WOLFSSH_NO_DH_GEX_SHA256
        case ID_DH_GEX_SHA256:
            return WC_HASH_TYPE_SHA256;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
        case ID_ECDH_SHA2_NISTP256:
            return WC_HASH_TYPE_SHA256;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
        case ID_ECDSA_SHA2_NISTP256:
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP256:
    #endif
            return WC_HASH_TYPE_SHA256;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
        case ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256:
            return WC_HASH_TYPE_SHA256;
#endif

        /* SHA2-384 */
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
        case ID_ECDH_SHA2_NISTP384:
            return WC_HASH_TYPE_SHA384;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
        case ID_ECDSA_SHA2_NISTP384:
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP384:
    #endif
            return WC_HASH_TYPE_SHA384;
#endif

        /* SHA2-512 */
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
        case ID_ECDH_SHA2_NISTP521:
            return WC_HASH_TYPE_SHA512;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
        case ID_ECDSA_SHA2_NISTP521:
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP521:
    #endif
            return WC_HASH_TYPE_SHA512;
#endif
        default:
            return WC_HASH_TYPE_NONE;
    }
}


#if !defined(WOLFSSH_NO_ECDSA) || !defined(WOLFSSH_NO_ECDH)
static INLINE int wcPrimeForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
        case ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256:
            return ECC_SECP256R1;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
        case ID_ECDH_SHA2_NISTP256:
            return ECC_SECP256R1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
        case ID_ECDSA_SHA2_NISTP256:
            return ECC_SECP256R1;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
        case ID_ECDH_SHA2_NISTP384:
            return ECC_SECP384R1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
        case ID_ECDSA_SHA2_NISTP384:
            return ECC_SECP384R1;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
        case ID_ECDH_SHA2_NISTP521:
            return ECC_SECP521R1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
        case ID_ECDSA_SHA2_NISTP521:
            return ECC_SECP521R1;
#endif
        default:
            return ECC_CURVE_INVALID;
    }
}

static INLINE const char *PrimeNameForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
        case ID_ECDSA_SHA2_NISTP256:
            return "nistp256";
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
        case ID_ECDSA_SHA2_NISTP384:
            return "nistp384";
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
        case ID_ECDSA_SHA2_NISTP521:
            return "nistp521";
#endif
        default:
            return "unknown";
    }
}
#endif /* WOLFSSH_NO_ECDSA */


static INLINE byte AeadModeForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_AES_GCM
        case ID_AES128_GCM:
        case ID_AES192_GCM:
        case ID_AES256_GCM:
            return 1;
#endif
        default:
            return 0;
    }
}


/* We have pairs of PubKey types that use the same signature,
 * i.e. ecdsa-sha2-nistp256 and x509v3-ecdsa-sha2-nistp256. */
static INLINE byte SigTypeForId(byte id)
{
    switch (id) {
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_SSH_RSA:
    #endif
        case ID_SSH_RSA:
            return ID_SSH_RSA;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP256:
    #endif
        case ID_ECDSA_SHA2_NISTP256:
            return ID_ECDSA_SHA2_NISTP256;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP384:
    #endif
        case ID_ECDSA_SHA2_NISTP384:
            return ID_ECDSA_SHA2_NISTP384;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP521:
    #endif
        case ID_ECDSA_SHA2_NISTP521:
            return ID_ECDSA_SHA2_NISTP521;
#endif
        default:
            return 0;
    }
}


static int DoKexInit(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;
    int side = WOLFSSH_ENDPOINT_SERVER;
    byte algoId;
    byte list[16] = {ID_NONE};
    word32 listSz;
    word32 skipSz;
    word32 begin;

    WLOG(WS_LOG_DEBUG, "Entering DoKexInit()");

    if (ssh == NULL || ssh->ctx == NULL ||
            buf == NULL || len == 0 || idx == NULL) {

        ret = WS_BAD_ARGUMENT;
    }

    /*
     * I don't need to save what the client sends here. I should decode
     * each list into a local array of IDs, and pick the one the peer is
     * using that's on my known list, or verify that the one the peer can
     * support the other direction is on my known list. All I need to do
     * is save the actual values.
     */

    if (ret == WS_SUCCESS) {
        if (ssh->handshake == NULL) {
            ssh->handshake = HandshakeInfoNew(ssh->ctx->heap);
            if (ssh->handshake == NULL) {
                WLOG(WS_LOG_DEBUG, "Couldn't allocate handshake info");
                ret = WS_MEMORY_E;
            }
        }
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        side = ssh->ctx->side;

        /* Check that the cookie exists inside the message */
        if (begin + COOKIE_SZ > len) {
            /* error, out of bounds */
            ret = WS_PARSE_E;
        }
        else {
            /* Move past the cookie. */
            begin += COOKIE_SZ;
        }
    }

    /* KEX Algorithms */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: KEX Algorithms");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            ssh->handshake->kexIdGuess = list[0];
            algoId = MatchIdLists(side, list, listSz,
                    cannedKexAlgo, cannedKexAlgoSz);
            if (algoId == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate KEX Algo");
                ret = WS_MATCH_KEX_ALGO_E;
            }
            else {
                ssh->handshake->kexId = algoId;
                ssh->handshake->hashId = HashForId(algoId);
            }
        }
    }

    /* Server Host Key Algorithms */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Server Host Key Algorithms");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            const byte *cannedKeyAlgo = NULL;
            word32 cannedKeyAlgoSz = 0;

            if (side == WOLFSSH_ENDPOINT_SERVER) {
                cannedKeyAlgo = ssh->ctx->privateKeyId;
                cannedKeyAlgoSz = ssh->ctx->privateKeyCount;
            }
            else {
                /* XXX Does this need to be different for client? */
                cannedKeyAlgo = cannedKeyAlgoClient;
                cannedKeyAlgoSz = cannedKeyAlgoClientSz;
            }
            algoId = MatchIdLists(side, list, listSz,
                                  cannedKeyAlgo, cannedKeyAlgoSz);
            if (algoId == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate Server Host Key Algo");
                return WS_MATCH_KEY_ALGO_E;
            }
            else {
                ssh->handshake->pubKeyId = algoId;
                ssh->handshake->sigId = SigTypeForId(algoId);
            }
        }
    }

    /* Enc Algorithms - Client to Server */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Enc Algorithms - Client to Server");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            algoId = MatchIdLists(side, list, listSz,
                    cannedEncAlgo, cannedEncAlgoSz);
            if (algoId == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate Encryption Algo C2S");
                ret = WS_MATCH_ENC_ALGO_E;
            }
        }
    }

    /* Enc Algorithms - Server to Client */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Enc Algorithms - Server to Client");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (MatchIdLists(side, list, listSz, &algoId, 1) == ID_UNKNOWN) {
            WLOG(WS_LOG_DEBUG, "Unable to negotiate Encryption Algo S2C");
            ret = WS_MATCH_ENC_ALGO_E;
        }
        else {
            ssh->handshake->encryptId = algoId;
            ssh->handshake->aeadMode = AeadModeForId(algoId);
            ssh->handshake->blockSz = BlockSzForId(algoId);
            ssh->handshake->keys.encKeySz =
                ssh->handshake->peerKeys.encKeySz =
                KeySzForId(algoId);
            if (!ssh->handshake->aeadMode) {
                ssh->handshake->keys.ivSz =
                    ssh->handshake->peerKeys.ivSz =
                    ssh->handshake->blockSz;
            }
            else {
#ifndef WOLFSSH_NO_AEAD
                ssh->handshake->keys.ivSz =
                    ssh->handshake->peerKeys.ivSz =
                    AEAD_NONCE_SZ;
                ssh->handshake->macSz = ssh->handshake->blockSz;
#endif
            }
        }
    }

    /* MAC Algorithms - Client to Server */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: MAC Algorithms - Client to Server");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS && !ssh->aeadMode) {
            algoId = MatchIdLists(side, list, listSz,
                    cannedMacAlgo, cannedMacAlgoSz);
            if (algoId == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate MAC Algo C2S");
                ret = WS_MATCH_MAC_ALGO_E;
            }
        }
    }

    /* MAC Algorithms - Server to Client */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: MAC Algorithms - Server to Client");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS && !ssh->handshake->aeadMode) {
            if (MatchIdLists(side, list, listSz, &algoId, 1) == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate MAC Algo S2C");
                ret = WS_MATCH_MAC_ALGO_E;
            }
            else {
                ssh->handshake->macId = algoId;
                ssh->handshake->macSz = MacSzForId(algoId);
                ssh->handshake->keys.macKeySz =
                    ssh->handshake->peerKeys.macKeySz =
                    KeySzForId(algoId);
            }
        }
    }

    /* Compression Algorithms - Client to Server */
    if (ret == WS_SUCCESS) {
        /* The compression algorithm lists should have none as a value. */
        algoId = ID_NONE;

        WLOG(WS_LOG_DEBUG, "DKI: Compression Algorithms - Client to Server");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            if (MatchIdLists(side, list, listSz, &algoId, 1) == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate Compression Algo C2S");
                ret = WS_INVALID_ALGO_ID;
            }
        }
    }

    /* Compression Algorithms - Server to Client */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Compression Algorithms - Server to Client");
        listSz = sizeof(list);
        ret = GetNameList(list, &listSz, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            if (MatchIdLists(side, list, listSz, &algoId, 1) == ID_UNKNOWN) {
                WLOG(WS_LOG_DEBUG, "Unable to negotiate Compression Algo S2C");
                ret = WS_INVALID_ALGO_ID;
            }
        }
    }

    /* Languages - Client to Server, skip */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Languages - Client to Server");
        ret = GetUint32(&skipSz, buf, len, &begin);
        if (ret == WS_SUCCESS)
            begin += skipSz;
    }

    /* Languages - Server to Client, skip */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: Languages - Server to Client");
        ret = GetUint32(&skipSz, buf, len, &begin);
        if (ret == WS_SUCCESS)
            begin += skipSz;
    }

    /* First KEX Packet Follows */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: KEX Packet Follows");
        ret = GetBoolean(&ssh->handshake->kexPacketFollows, buf, len, &begin);
        if (ret == WS_SUCCESS) {
            WLOG(WS_LOG_DEBUG, " packet follows: %s",
                    ssh->handshake->kexPacketFollows ? "yes" : "no");
        }
    }

    /* Skip the "for future use" length. */
    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DKI: For Future Use");
        ret = GetUint32(&skipSz, buf, len, &begin);
        if (ret == WS_SUCCESS)
            begin += skipSz;
    }

    if (ret == WS_SUCCESS) {
        enum wc_HashType enmhashId = (enum wc_HashType)ssh->handshake->hashId;
        byte scratchLen[LENGTH_SZ];
        word32 strSz;

        if (!ssh->isKeying) {
            WLOG(WS_LOG_DEBUG, "Keying initiated");
            ret = SendKexInit(ssh);
        }

        /* account for possible want write case from SendKexInit */
        if (ret == WS_SUCCESS || ret == WS_WANT_WRITE)
            ret = wc_HashInit(&ssh->handshake->hash, enmhashId);

        if (ret == WS_SUCCESS) {
            if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER) {
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    ssh->peerProtoId, ssh->peerProtoIdSz);
            }
        }

        if (ret == WS_SUCCESS) {
            byte SSH_PROTO_EOL_SZ = 2;

            strSz = (word32)WSTRLEN(sshProtoIdStr) - SSH_PROTO_EOL_SZ;
            c32toa(strSz, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, LENGTH_SZ);
        }

        if (ret == WS_SUCCESS) {
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                (const byte*)sshProtoIdStr, strSz);
        }

        if (ret == WS_SUCCESS) {
            if (ssh->ctx->side == WOLFSSH_ENDPOINT_CLIENT) {
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    ssh->peerProtoId, ssh->peerProtoIdSz);
                if (ret == WS_SUCCESS) {
                    ret = HashUpdate(&ssh->handshake->hash,
                                        enmhashId,
                                        ssh->handshake->kexInit,
                                        ssh->handshake->kexInitSz);
                }
            }
        }

        if (ret == WS_SUCCESS) {
            c32toa(len + 1, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, LENGTH_SZ);
        }

        if (ret == WS_SUCCESS) {
            scratchLen[0] = MSGID_KEXINIT;
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, MSG_ID_SZ);
        }

        if (ret == WS_SUCCESS)
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                buf, len);

        if (ret == WS_SUCCESS) {
            if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER)
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    ssh->handshake->kexInit,
                                    ssh->handshake->kexInitSz);
        }

        if (ret == WS_SUCCESS) {
            *idx = begin;
            if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER)
                ssh->clientState = CLIENT_KEXINIT_DONE;
            else
                ssh->serverState = SERVER_KEXINIT_DONE;

            if (ssh->error != 0)
                ret = ssh->error; /* propogate potential want write case from
                                     SendKexInit*/
        }
    }
    WLOG(WS_LOG_DEBUG, "Leaving DoKexInit(), ret = %d", ret);
    return ret;
}


/* create mpint type
 *
 * can decrease size of buf by 1 or more if leading bytes are 0's and not needed
 * the input argument "sz" gets reset if that is the case. Buffer size is never
 * increased.
 *
 * An example of this would be a buffer of 0053 changed to 53.
 * If a padding value is needed then "pad" is set to 1
 *
 */
static int CreateMpint(byte* buf, word32* sz, byte* pad)
{
    word32 i;

    if (buf == NULL || sz == NULL || pad == NULL) {
        WLOG(WS_LOG_ERROR, "Internal argument error with CreateMpint");
        return WS_BAD_ARGUMENT;
    }

    if (*sz == 0)
        return WS_SUCCESS;

    /* check for leading 0's */
    for (i = 0; i < *sz; i++) {
        if (buf[i] != 0x00)
            break;
    }
    *pad = (buf[i] & 0x80) ? 1 : 0;

    /* if padding would be needed and have leading 0's already then do not add
     * extra 0's */
    if (i > 0 && *pad == 1) {
        i = i - 1;
        *pad = 0;
    }

    /* if i is still greater than 0 then the buffer needs shifted to remove
     * leading 0's */
    if (i > 0) {
        WMEMMOVE(buf, buf + i, *sz - i);
        *sz = *sz - i;
    }

    return WS_SUCCESS;
}


#if !defined(WOLFSSH_NO_DH_GROUP1_SHA1) || \
    !defined(WOLFSSH_NO_DH_GROUP14_SHA1) || \
    !defined(WOLFSSH_NO_DH_GEX_SHA256)
static const byte dhGenerator[] = { 2 };
static const word32 dhGeneratorSz = sizeof(dhGenerator);
#endif

#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
static const byte dhPrimeGroup1[] = {
    /* SSH DH Group 1 (Oakley Group 2, 1024-bit MODP Group, RFC 2409) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const word32 dhPrimeGroup1Sz = sizeof(dhPrimeGroup1);
#endif

#if !defined(WOLFSSH_NO_DH_GROUP14_SHA1) || \
    !defined(WOLFSSH_NO_DH_GEX_SHA256)
static const byte dhPrimeGroup14[] = {
    /* SSH DH Group 14 (Oakley Group 14, 2048-bit MODP Group, RFC 3526) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
    0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
    0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A,
    0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
    0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96,
    0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
    0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
    0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
    0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C,
    0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B,
    0xE3, 0x9E, 0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03,
    0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F,
    0xB5, 0xC5, 0x5D, 0xF0, 0x6F, 0x4C, 0x52, 0xC9,
    0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18,
    0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95, 0x6A, 0xE5,
    0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10,
    0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAC, 0xAA, 0x68,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const word32 dhPrimeGroup14Sz = sizeof(dhPrimeGroup14);
#endif


static int DoKexDhInit(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    /* First get the length of the MP_INT, and then add in the hash of the
     * mp_int value of e as it appears in the packet. After that, decode e
     * into an mp_int struct for the DH calculation by wolfCrypt.
     *
     * This function also works as MSGID_KEXECDH_INIT (30). That message
     * has the same format as MSGID_KEXDH_INIT, except it is the ECDH Q value
     * in the message isn't of the DH e value. Treat the Q as e. */
    /* DYNTYPE_DH */

    byte* e;
    word32 eSz;
    word32 begin;
    int ret = WS_SUCCESS;

    if (ssh == NULL || ssh->handshake == NULL || buf == NULL || len == 0 ||
            idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        if (ssh->handshake->kexPacketFollows
                && ssh->handshake->kexIdGuess != ssh->handshake->kexId) {

            /* skip this message. */
            WLOG(WS_LOG_DEBUG, "Skipping the client's KEX init function.");
            ssh->handshake->kexPacketFollows = 0;
            *idx += len;
            return WS_SUCCESS;
        }
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetUint32(&eSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        /* Validate eSz */
        if ((len < begin) || (eSz > len - begin)) {
            ret = WS_RECV_OVERFLOW_E;
        }
    }

    if (ret == WS_SUCCESS) {
        e = buf + begin;
        begin += eSz;

        if (eSz <= sizeof(ssh->handshake->e)) {
            WMEMCPY(ssh->handshake->e, e, eSz);
            ssh->handshake->eSz = eSz;
        }

        ssh->clientState = CLIENT_KEXDH_INIT_DONE;
        *idx = begin;

        ret = SendKexDhReply(ssh);
    }

    return ret;
}


struct wolfSSH_sigKeyBlock {
    byte useRsa;
    word32 keySz;
    union {
#ifndef WOLFSSH_NO_RSA
        struct {
            RsaKey   key;
        } rsa;
#endif
#ifndef WOLFSSH_NO_ECDSA
        struct {
            ecc_key key;
        } ecc;
#endif
    } sk;
};


/* Parse out a RAW RSA public key from buffer */
static int ParseRSAPubKey(WOLFSSH *ssh,
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr, byte *pubKey, word32 pubKeySz)
{
    int ret;
#ifndef WOLFSSH_NO_RSA
    byte* e = NULL;
    word32 eSz;
    byte* n;
    word32 nSz;
    word32 pubKeyIdx = 0;
    word32 scratch;

    ret = wc_InitRsaKey(&sigKeyBlock_ptr->sk.rsa.key, ssh->ctx->heap);
    if (ret != 0)
        ret = WS_RSA_E;
    if (ret == 0)
        ret = GetUint32(&scratch, pubKey, pubKeySz, &pubKeyIdx);
    /* This is the algo name. */
    if (ret == WS_SUCCESS) {
        pubKeyIdx += scratch;
        ret = GetUint32(&eSz, pubKey, pubKeySz, &pubKeyIdx);
        if (ret == WS_SUCCESS && eSz > pubKeySz - pubKeyIdx)
            ret = WS_BUFFER_E;
    }
    if (ret == WS_SUCCESS) {
        e = pubKey + pubKeyIdx;
        pubKeyIdx += eSz;
        ret = GetUint32(&nSz, pubKey, pubKeySz, &pubKeyIdx);
        if (ret == WS_SUCCESS && nSz > pubKeySz - pubKeyIdx)
            ret = WS_BUFFER_E;
    }
    if (ret == WS_SUCCESS) {
        n = pubKey + pubKeyIdx;
        ret = wc_RsaPublicKeyDecodeRaw(n, nSz, e, eSz,
                                       &sigKeyBlock_ptr->sk.rsa.key);
    }

    if (ret == 0)
        sigKeyBlock_ptr->keySz = sizeof(sigKeyBlock_ptr->sk.rsa.key);
    else
        ret = WS_RSA_E;
#else
    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(sigKeyBlock_ptr);
    WOLFSSH_UNUSED(pubKey);
    WOLFSSH_UNUSED(pubKeySz);
    ret = WS_INVALID_ALGO_ID;
#endif
    return ret;
}

/* Parse out a RAW ECC public key from buffer */
static int ParseECCPubKey(WOLFSSH *ssh,
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr, byte *pubKey, word32 pubKeySz)
{
    int ret;
#ifndef WOLFSSH_NO_ECDSA
    const byte* q;
    word32 qSz, pubKeyIdx = 0;
    int primeId;
    word32 scratch;

    ret = wc_ecc_init_ex(&sigKeyBlock_ptr->sk.ecc.key, ssh->ctx->heap,
                                 INVALID_DEVID);
#ifdef HAVE_WC_ECC_SET_RNG
    if (ret == 0)
        ret = wc_ecc_set_rng(&sigKeyBlock_ptr->sk.ecc.key, ssh->rng);
#endif
    if (ret != 0)
        ret = WS_ECC_E;
    else
        ret = GetStringRef(&qSz, &q, pubKey, pubKeySz, &pubKeyIdx);

    if (ret == WS_SUCCESS) {
        primeId = (int)NameToId((const char*)q, qSz);
        if (primeId != ID_UNKNOWN) {
            primeId = wcPrimeForId((byte)primeId);
            if (primeId == ECC_CURVE_INVALID)
                ret = WS_INVALID_PRIME_CURVE;
        }
        else
            ret = WS_INVALID_ALGO_ID;
    }

    /* Skip the curve name since we're getting it from the algo. */
    if (ret == WS_SUCCESS)
        ret = GetUint32(&scratch, pubKey, pubKeySz, &pubKeyIdx);

    if (ret == WS_SUCCESS) {
        pubKeyIdx += scratch;
        ret = GetStringRef(&qSz, &q, pubKey, pubKeySz, &pubKeyIdx);
    }

    if (ret == WS_SUCCESS) {
        ret = wc_ecc_import_x963_ex(q, qSz,
                &sigKeyBlock_ptr->sk.ecc.key, primeId);
        if (ret == 0)
            sigKeyBlock_ptr->keySz = sizeof(sigKeyBlock_ptr->sk.ecc.key);
        else
            ret = WS_ECC_E;
    }
#else
    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(sigKeyBlock_ptr);
    WOLFSSH_UNUSED(pubKey);
    WOLFSSH_UNUSED(pubKeySz);
    ret = WS_INVALID_ALGO_ID;
#endif
    return ret;
}


#ifdef WOLFSSH_CERTS
/* finds the leaf certificate and verifies it with known CA's
 * returns WS_SUCCESS on success */
static int ParseAndVerifyCert(WOLFSSH* ssh, byte* in, word32 inSz,
    byte** leafOut, word32* leafOutSz)
{
    int ret;
    word32 l = 0, m = 0;
    word32 ocspCount = 0;
    word32 certCount = 0;
    byte*  certChain = NULL;
    word32 certChainSz = 0;
    word32 count;

    /* Skip the name */
    ret = GetSize(&l, in, inSz, &m);
    m += l;

    if (ret == WS_SUCCESS) {
        /* Get the cert count */
        ret = GetUint32(&certCount, in, inSz, &m);
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "Peer sent certificate count of %d", certCount);
        certChain = in + m;

        for (count = certCount; count > 0; count--) {
            word32 certSz = 0;

            ret = GetSize(&certSz, in, inSz, &m);
            if (ret != WS_SUCCESS) {
                break;
            }
            WLOG(WS_LOG_INFO, "Adding certificate size %u", certSz);

            /* store leaf cert size to present to user callback */
            if (count == certCount && leafOut != NULL) {
                *leafOutSz = certSz;
                *leafOut   = in + m;
            }
            certChainSz += certSz + UINT32_SZ;
            m += certSz;
        }

        /* get OCSP count */
        if (ret == WS_SUCCESS) {
            ret = GetUint32(&ocspCount, in, inSz, &m);
        }

        if (ret == WS_SUCCESS) {
            WLOG(WS_LOG_INFO, "Peer sent OCSP count of %u", ocspCount);

            /* RFC 6187 section 2.1 OCSP count must not exceed cert count */
            if (ocspCount > certCount) {
                WLOG(WS_LOG_ERROR, "Error more OCSP then Certs");
                ret = WS_FATAL_ERROR;
            }
        }

        if (ret == WS_SUCCESS) {
            /* @TODO handle OCSP's */
            if (ocspCount > 0) {
                WLOG(WS_LOG_INFO, "Peer sent OCSP's, not yet handled");
            }
        }
    }

    /* verify the certificate chain */
    if (ret == WS_SUCCESS) {
        ret = wolfSSH_CERTMAN_VerifyCerts_buffer(ssh->ctx->certMan,
                    certChain, certChainSz, certCount);
    }

    return ret;
}


/* finds the leaf certificate after having been verified, and extracts the
 * public key in DER format from it
 * this function allocates 'out' buffer, it is up to the caller to free it
 * return WS_SUCCESS on success */
static int ParsePubKeyCert(WOLFSSH* ssh, byte* in, word32 inSz, byte** out,
    word32* outSz)
{
    int ret;
    byte*  leaf   = NULL;
    word32 leafSz = 0;

    ret = ParseAndVerifyCert(ssh, in, inSz, &leaf, &leafSz);
    if (ret == WS_SUCCESS) {
        int error = 0;
        struct DecodedCert dCert;

        wc_InitDecodedCert(&dCert, leaf, leafSz, ssh->ctx->heap);
        error = wc_ParseCert(&dCert, CERT_TYPE, 0, NULL);
        if (error == 0) {
            error = wc_GetPubKeyDerFromCert(&dCert, *out, outSz);
            if (error == LENGTH_ONLY_E) {
                error = 0;
                *out = (byte*)WMALLOC(*outSz, NULL, 0);
                if (*out == NULL) {
                    error = WS_MEMORY_E;
                }
            }

            if (error == 0) {
                error = wc_GetPubKeyDerFromCert(&dCert, *out, outSz);
                if (error != 0) {
                    WFREE(*out, NULL, 0);
                }
            }
        }
        wc_FreeDecodedCert(&dCert);

        if (error != 0) {
            ret = error;
        }
    }

    return ret;
}


/* return WS_SUCCESS on success */
static int ParseECCPubKeyCert(WOLFSSH *ssh,
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr, byte *pubKey, word32 pubKeySz)
{
    int ret;
#ifndef WOLFSSH_NO_ECDSA
    byte* der = NULL;
    word32 derSz, idx = 0;
    int error;

    ret = ParsePubKeyCert(ssh, pubKey, pubKeySz, &der, &derSz);
    if (ret == WS_SUCCESS) {
        error = wc_ecc_init_ex(&sigKeyBlock_ptr->sk.ecc.key, ssh->ctx->heap,
                                 INVALID_DEVID);
    #ifdef HAVE_WC_ECC_SET_RNG
        if (error == 0)
            error = wc_ecc_set_rng(&sigKeyBlock_ptr->sk.ecc.key, ssh->rng);
    #endif
        if (error == 0)
            error = wc_EccPublicKeyDecode(der, &idx,
                &sigKeyBlock_ptr->sk.ecc.key, derSz);
        if (error == 0)
            sigKeyBlock_ptr->keySz = sizeof(sigKeyBlock_ptr->sk.ecc.key);
        if (error != 0)
            ret = error;
        WFREE(der, NULL, 0);
    }
#else
    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(sigKeyBlock_ptr);
    WOLFSSH_UNUSED(pubKey);
    WOLFSSH_UNUSED(pubKeySz);
    ret = WS_INVALID_ALGO_ID;
#endif

    return ret;
}


/* return WS_SUCCESS on success */
static int ParseRSAPubKeyCert(WOLFSSH *ssh,
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr, byte *pubKey, word32 pubKeySz)
{

    int ret;
#ifndef WOLFSSH_NO_RSA
    byte* der = NULL;
    word32 derSz, idx = 0;
    int error;

    ret = ParsePubKeyCert(ssh, pubKey, pubKeySz, &der, &derSz);
    if (ret == WS_SUCCESS) {
        error = wc_InitRsaKey(&sigKeyBlock_ptr->sk.rsa.key, ssh->ctx->heap);
        if (error == 0)
            error = wc_RsaPublicKeyDecode(der, &idx,
                                          &sigKeyBlock_ptr->sk.rsa.key, derSz);
        if (error == 0)
            sigKeyBlock_ptr->keySz = sizeof(sigKeyBlock_ptr->sk.rsa.key);
        if (error != 0)
            ret = error;
        WFREE(der, NULL, 0);
    }
#else
    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(sigKeyBlock_ptr);
    WOLFSSH_UNUSED(pubKey);
    WOLFSSH_UNUSED(pubKeySz);
    ret = WS_INVALID_ALGO_ID;
#endif

    return ret;
}
#endif /* WOLFSSH_CERTS */


/* Parse out a public key from buffer received
 * return WS_SUCCESS on success */
static int ParsePubKey(WOLFSSH *ssh,
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr, byte *pubKey, word32 pubKeySz)
{
    int ret;

    switch (ssh->handshake->pubKeyId) {
        case ID_SSH_RSA:
            sigKeyBlock_ptr->useRsa = 1;
            ret = ParseRSAPubKey(ssh, sigKeyBlock_ptr, pubKey, pubKeySz);
            break;

    #ifdef WOLFSSH_CERTS
        case ID_X509V3_SSH_RSA:
            sigKeyBlock_ptr->useRsa = 1;
            ret = ParseRSAPubKeyCert(ssh, sigKeyBlock_ptr, pubKey, pubKeySz);
            break;
    #endif

        case ID_ECDSA_SHA2_NISTP256:
        case ID_ECDSA_SHA2_NISTP384:
        case ID_ECDSA_SHA2_NISTP521:
            sigKeyBlock_ptr->useRsa = 0;
            ret = ParseECCPubKey(ssh, sigKeyBlock_ptr, pubKey, pubKeySz);
            break;

    #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP256:
        case ID_X509V3_ECDSA_SHA2_NISTP384:
        case ID_X509V3_ECDSA_SHA2_NISTP521:
            sigKeyBlock_ptr->useRsa = 0;
            ret = ParseECCPubKeyCert(ssh, sigKeyBlock_ptr, pubKey, pubKeySz);
            break;
    #endif

        default:
            ret = WS_INVALID_ALGO_ID;
    }

    return ret;
}


static int DoKexDhReply(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    enum wc_HashType enmhashId;
    byte* pubKey = NULL;
    word32 pubKeySz;
    byte* f = NULL;
    word32 fSz;
    byte* sig;
    word32 sigSz;
    word32 scratch;
    byte  scratchLen[LENGTH_SZ];
    byte  kPad = 0;
    word32 begin;
    int ret = WS_SUCCESS;
    struct wolfSSH_sigKeyBlock *sigKeyBlock_ptr = NULL;
    byte keyAllocated = 0;
#ifndef WOLFSSH_NO_ECDH
    ecc_key *key_ptr = NULL;
    #ifndef WOLFSSH_SMALL_STACK
        ecc_key key_s;
    #endif
#endif

    WLOG(WS_LOG_DEBUG, "Entering DoKexDhReply()");

    if (ssh == NULL || ssh->handshake == NULL || buf == NULL ||
            len == 0 || idx == NULL) {
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving DoKexDhReply(), ret = %d", ret);
        return ret;
    }

    if (ret == WS_SUCCESS && len < LENGTH_SZ*2 + *idx) {
        ret = WS_BUFFER_E;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetUint32(&pubKeySz, buf, len, &begin);
        if (ret == WS_SUCCESS && (pubKeySz > len - LENGTH_SZ - begin )) {
            ret = WS_BUFFER_E;
        }
    }

    if (ret == WS_SUCCESS) {
        pubKey = buf + begin;
        if (ssh->ctx->publicKeyCheckCb != NULL) {
            WLOG(WS_LOG_DEBUG, "DKDR: Calling the public key check callback");
            ret = ssh->ctx->publicKeyCheckCb(pubKey, pubKeySz,
                    ssh->publicKeyCheckCtx);
            if (ret == 0) {
                WLOG(WS_LOG_DEBUG, "DKDR: public key accepted");
                ret = WS_SUCCESS;
            }
            else {
                WLOG(WS_LOG_DEBUG, "DKDR: public key rejected");
                ret = WS_PUBKEY_REJECTED_E;
            }
        }
        else {
            WLOG(WS_LOG_DEBUG, "DKDR: no public key check callback, accepted");
            ret = WS_SUCCESS;
        }
    }

    enmhashId = (enum wc_HashType)ssh->handshake->hashId;

    if (ret == WS_SUCCESS) {
        /* Hash in the raw public key blob from the server including its
         * length which is at LENGTH_SZ offset ahead of pubKey. */
        ret = HashUpdate(&ssh->handshake->hash,
                            enmhashId,
                            pubKey - LENGTH_SZ, pubKeySz + LENGTH_SZ);
    }

    if (ret == WS_SUCCESS)
        begin += pubKeySz;

#ifndef WOLFSSH_NO_DH_GEX_SHA256
    /* If using DH-GEX include the GEX specific values. */
    if (ret == WS_SUCCESS && ssh->handshake->kexId == ID_DH_GEX_SHA256) {
        byte primeGroupPad = 0, generatorPad = 0;

        if (ssh->handshake->primeGroup == NULL ||
                ssh->handshake->generator == NULL) {
            WLOG(WS_LOG_DEBUG,
                    "DKDR: trying GEX without generator or prime group");
            ret = WS_BAD_ARGUMENT;
        }

        /* Hash in the client's requested minimum key size. */
        if (ret == 0) {
            c32toa(ssh->handshake->dhGexMinSz, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Hash in the client's requested preferred key size. */
        if (ret == 0) {
            c32toa(ssh->handshake->dhGexPreferredSz, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Hash in the client's requested maximum key size. */
        if (ret == 0) {
            c32toa(ssh->handshake->dhGexMaxSz, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Add a pad byte if the mpint has the MSB set. */
        if (ret == 0) {
            if (ssh->handshake->primeGroup != NULL &&
                    ssh->handshake->primeGroup[0] & 0x80)
                primeGroupPad = 1;

            /* Hash in the length of the GEX prime group. */
            c32toa(ssh->handshake->primeGroupSz + primeGroupPad,
                   scratchLen);
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Hash in the pad byte for the GEX prime group. */
        if (ret == 0) {
            if (primeGroupPad) {
                scratchLen[0] = 0;
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, 1);
            }
        }
        /* Hash in the GEX prime group. */
        if (ret == 0)
            ret  = HashUpdate(&ssh->handshake->hash,
                                 enmhashId,
                                 ssh->handshake->primeGroup,
                                 ssh->handshake->primeGroupSz);
        /* Add a pad byte if the mpint has the MSB set. */
        if (ret == 0) {
            if (ssh->handshake->generator[0] & 0x80)
                generatorPad = 1;

            /* Hash in the length of the GEX generator. */
            c32toa(ssh->handshake->generatorSz + generatorPad, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Hash in the pad byte for the GEX generator. */
        if (ret == 0) {
            if (generatorPad) {
                scratchLen[0] = 0;
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, 1);
            }
        }
        /* Hash in the GEX generator. */
        if (ret == 0)
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId,
                                ssh->handshake->generator,
                                ssh->handshake->generatorSz);
    }
#endif

    /* Hash in the size of the client's DH e-value (ECDH Q-value). */
    if (ret == 0) {
        c32toa(ssh->handshake->eSz, scratchLen);
        ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                            scratchLen, LENGTH_SZ);
    }
    /* Hash in the client's DH e-value (ECDH Q-value). */
    if (ret == 0)
        ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                            ssh->handshake->e, ssh->handshake->eSz);

    /* Get and hash in the server's DH f-value (ECDH Q-value) */
    if (ret == WS_SUCCESS) {
        f = buf + begin;
        ret = GetUint32(&fSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        if (fSz > len - begin) {
            WLOG(WS_LOG_DEBUG, "F size would result in error");
            ret = WS_PARSE_E;
        }
    }

    if (ret == WS_SUCCESS) {
        ret = HashUpdate(&ssh->handshake->hash,
                            enmhashId,
                            f, fSz + LENGTH_SZ);
    }

    if (ret == WS_SUCCESS) {
        f = buf + begin;
        begin += fSz;
        ret = GetUint32(&sigSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        if (sigSz > len - begin) {
            WLOG(WS_LOG_DEBUG, "Signature size would result in error 1");
            ret = WS_PARSE_E;
        }
    }

    if (ret == WS_SUCCESS) {
        sigKeyBlock_ptr = (struct wolfSSH_sigKeyBlock*)WMALLOC(
                sizeof(struct wolfSSH_sigKeyBlock), ssh->ctx->heap,
                DYNTYPE_PRIVKEY);
        if (sigKeyBlock_ptr == NULL) {
            ret = WS_MEMORY_E;
        }

#ifdef WOLFSSH_SMALL_STACK
#ifndef WOLFSSH_NO_ECDSA
        key_ptr = (ecc_key*)WMALLOC(sizeof(ecc_key), ssh->ctx->heap,
                DYNTYPE_PRIVKEY);
        if (key_ptr == NULL) {
            ret = WS_MEMORY_E;
        }
#endif /* WOLFSSH_NO_ECDSA */

#else /* ! WOLFSSH_SMALL_STACK */
#ifndef WOLFSSH_NO_ECDSA
        key_ptr = &key_s;
#endif
#endif
    }

    if (ret == WS_SUCCESS) {
        sig = buf + begin;
        begin += sigSz;
        *idx = begin;

        ret = ParsePubKey(ssh, sigKeyBlock_ptr, pubKey, pubKeySz);
        /* Generate and hash in the shared secret */
        if (ret == WS_SUCCESS) {
            /* Remember that the key needs to be freed */
            keyAllocated = 1;
            /* reset size here because a previous shared secret could
             * potentially be smaller by a byte than usual and cause buffer
             * issues with re-key */
            ssh->kSz = MAX_KEX_KEY_SZ;
            if (!ssh->handshake->useEcc
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
                && !ssh->handshake->useEccKyber
#endif
               ) {
#ifndef WOLFSSH_NO_DH
                PRIVATE_KEY_UNLOCK();
                ret = wc_DhAgree(&ssh->handshake->privKey.dh,
                                 ssh->k, &ssh->kSz,
                                 ssh->handshake->x, ssh->handshake->xSz,
                                 f, fSz);
                PRIVATE_KEY_LOCK();
                ForceZero(ssh->handshake->x, ssh->handshake->xSz);
                wc_FreeDhKey(&ssh->handshake->privKey.dh);
                if (ret != 0) {
                    WLOG(WS_LOG_ERROR,
                            "Generate DH shared secret failed, %d", ret);
                }
#else
                ret = WS_INVALID_ALGO_ID;
#endif
            }
            else if (ssh->handshake->useEcc) {
#ifndef WOLFSSH_NO_ECDH
                ret = wc_ecc_init(key_ptr);
#ifdef HAVE_WC_ECC_SET_RNG
                if (ret == 0)
                    ret = wc_ecc_set_rng(key_ptr, ssh->rng);
#endif
                if (ret == 0)
                    ret = wc_ecc_import_x963(f, fSz, key_ptr);
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_shared_secret(&ssh->handshake->privKey.ecc,
                                               key_ptr, ssh->k, &ssh->kSz);
                    PRIVATE_KEY_LOCK();
                }
                wc_ecc_free(key_ptr);
                wc_ecc_free(&ssh->handshake->privKey.ecc);
                if (ret != 0) {
                    WLOG(WS_LOG_ERROR,
                            "Generate ECC shared secret failed, %d", ret);
                }
#else
                ret = WS_INVALID_ALGO_ID;
#endif
            }
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
            else if (ssh->handshake->useEccKyber) {
                /* This is a a hybrid of ECDHE and a post-quantum KEM. In this
                 * case, I need to generated the ECC shared secret and
                 * decapsulate the ciphertext of the post-quantum KEM. */
                OQS_KEM* kem = OQS_KEM_new(OQS_KEM_alg_kyber_512);
                if (kem == NULL) {
                    ret = WS_INVALID_ALGO_ID;
                }

                if (fSz <= (word32)kem->length_ciphertext) {
                    ret = WS_BUFFER_E;
                }

                ret = wc_ecc_init(key_ptr);
#ifdef HAVE_WC_ECC_SET_RNG
                if (ret == 0)
                    ret = wc_ecc_set_rng(key_ptr, ssh->rng);
#endif
                if (ret == 0) {
                    ret = wc_ecc_import_x963(f, fSz -
                              (word32)kem->length_ciphertext, key_ptr);
                }

                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_shared_secret(&ssh->handshake->privKey.ecc,
                                               key_ptr, ssh->k, &ssh->kSz);
                    PRIVATE_KEY_LOCK();
                }
                wc_ecc_free(key_ptr);
                wc_ecc_free(&ssh->handshake->privKey.ecc);

                if (OQS_KEM_decaps(kem, ssh->k + ssh->kSz,
                        f + fSz - kem->length_ciphertext, ssh->handshake->x)
                    != OQS_SUCCESS) {
                    ret = WS_ERROR;
                }

                if (ret == 0) {
                    ssh->kSz += kem->length_shared_secret;
                } else {
                    ssh->kSz = 0;
                    WLOG(WS_LOG_ERROR,
                         "Generate ECC-kyber (decap) shared secret failed, %d",
                         ret);
                }

                if (kem != NULL) {
                    OQS_KEM_free(kem);
                }
            }
#endif
            else {
                ret = WS_INVALID_ALGO_ID;
            }
        }
        if (ret == 0)
            ret = CreateMpint(ssh->k, &ssh->kSz, &kPad);

        /* Hash in the shared secret K. */
        if (ret == 0) {
            c32toa(ssh->kSz + kPad, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        if (ret == 0) {
            if (kPad) {
                scratchLen[0] = 0;
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId, scratchLen, 1);
            }
        }
        if (ret == 0)
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                ssh->k, ssh->kSz);

        /* Save the exchange hash value H, and session ID. */
        if (ret == 0) {
            ret = wc_HashFinal(&ssh->handshake->hash,
                               enmhashId, ssh->h);
            wc_HashFree(&ssh->handshake->hash, enmhashId);
            ssh->handshake->hashId = WC_HASH_TYPE_NONE;
        }

        if (ret == 0) {
            ssh->hSz = wc_HashGetDigestSize(enmhashId);
            if (ssh->sessionIdSz == 0) {
                WMEMCPY(ssh->sessionId, ssh->h, ssh->hSz);
                ssh->sessionIdSz = ssh->hSz;
            }
        }

        if (ret != WS_SUCCESS)
            ret = WS_CRYPTO_FAILED;

        /* Verify h with the server's public key. */
        if (ret == WS_SUCCESS) {
#ifndef WOLFSSH_NO_RSA
        int tmpIdx = begin;
#endif
            /* Skip past the sig name. Check it, though. Other SSH
             * implementations do the verify based on the name, despite what
             * was agreed upon. XXX*/
            begin = 0;
            ret = GetUint32(&scratch, sig, sigSz, &begin);
            if (ret == WS_SUCCESS) {
                /* Check that scratch isn't larger than the remainder of the
                 * sig buffer and leaves enough room for another length. */
                if (scratch > sigSz - begin - LENGTH_SZ) {
                    WLOG(WS_LOG_DEBUG, "sig name size is too large");
                    ret = WS_PARSE_E;
                }
            }
            if (ret == WS_SUCCESS) {
                begin += scratch;
                ret = GetUint32(&scratch, sig, sigSz, &begin);
            }
            if (ret == WS_SUCCESS) {
                if (scratch > sigSz - begin) {
                    WLOG(WS_LOG_DEBUG, "sig name size is too large");
                    ret = WS_PARSE_E;
                }
            }
            if (ret == WS_SUCCESS) {
                if (sigKeyBlock_ptr->useRsa) {
#ifndef WOLFSSH_NO_RSA
                    sig = sig + begin;
                    /* In the fuzz, sigSz ends up 1 and it has issues. */
                    sigSz = scratch;

                    if (sigSz < MIN_RSA_SIG_SZ) {
                        WLOG(WS_LOG_DEBUG, "Provided signature is too small.");
                        ret = WS_RSA_E;
                    }

                    if (sigSz + begin + tmpIdx > len) {
                        WLOG(WS_LOG_DEBUG,
                                "Signature size found would result in error 2");
                        ret = WS_BUFFER_E;
                    }

                    if (ret == WS_SUCCESS) {
                        ret = wc_SignatureVerify(
                                         HashForId(ssh->handshake->pubKeyId),
                                         WC_SIGNATURE_TYPE_RSA_W_ENC,
                                         ssh->h, ssh->hSz, sig, sigSz,
                                         &sigKeyBlock_ptr->sk, sigKeyBlock_ptr->keySz);
                        if (ret != 0) {
                            WLOG(WS_LOG_DEBUG,
                                "DoKexDhReply: Signature Verify fail (%d)",
                                ret);
                            ret = WS_RSA_E;
                        }
                    }
#endif
                }
                else {
#ifndef WOLFSSH_NO_ECDSA
                    const byte* r;
                    const byte* s;
                    word32 rSz, sSz, asnSigSz;
                    byte asnSig[256];

                    sig = sig + begin;
                    sigSz = scratch;
                    begin = 0;
                    asnSigSz = sizeof(asnSig);
                    XMEMSET(asnSig, 0, asnSigSz);

                    ret = GetStringRef(&rSz, &r, sig, sigSz, &begin);
                    if (ret == WS_SUCCESS)
                        ret = GetStringRef(&sSz, &s, sig, sigSz, &begin);

                    if (ret == WS_SUCCESS)
                        ret = wc_ecc_rs_raw_to_sig(r, rSz, s, sSz,
                                asnSig, &asnSigSz);

                    if (ret == WS_SUCCESS) {
                        ret = wc_SignatureVerify(
                                         HashForId(ssh->handshake->pubKeyId),
                                         WC_SIGNATURE_TYPE_ECC,
                                         ssh->h, ssh->hSz, asnSig, asnSigSz,
                                         &sigKeyBlock_ptr->sk, sigKeyBlock_ptr->keySz);
                        if (ret != 0) {
                            WLOG(WS_LOG_DEBUG,
                                "DoKexDhReply: Signature Verify fail (%d)",
                                ret);
                            ret = WS_ECC_E;
                        }
                    }
#endif
                }
            }
        }

        if (keyAllocated) {
            if (sigKeyBlock_ptr->useRsa) {
#ifndef WOLFSSH_NO_RSA
                wc_FreeRsaKey(&sigKeyBlock_ptr->sk.rsa.key);
#endif
            }
            else {
#ifndef WOLFSSH_NO_ECDSA
                wc_ecc_free(&sigKeyBlock_ptr->sk.ecc.key);
#endif
            }
        }
    }

    if (ret == WS_SUCCESS)
        ret = GenerateKeys(ssh, enmhashId);

    if (ret == WS_SUCCESS)
        ret = SendNewKeys(ssh);

    if (sigKeyBlock_ptr)
        WFREE(sigKeyBlock_ptr, ssh->ctx->heap, DYNTYPE_PRIVKEY);
#ifdef WOLFSSH_SMALL_STACK
    #ifndef WOLFSSH_NO_ECDSA
    if (key_ptr)
        WFREE(key_ptr, ssh->ctx->heap, DYNTYPE_PRIVKEY);
    #endif
#endif
    WLOG(WS_LOG_DEBUG, "Leaving DoKexDhReply(), ret = %d", ret);
    return ret;
}


static int DoNewKeys(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;

    WOLFSSH_UNUSED(buf);
    WOLFSSH_UNUSED(len);
    WOLFSSH_UNUSED(idx);

    if (ssh == NULL || ssh->handshake == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        ssh->peerEncryptId = ssh->handshake->encryptId;
        ssh->peerMacId = ssh->handshake->macId;
        ssh->peerBlockSz = ssh->handshake->blockSz;
        ssh->peerMacSz = ssh->handshake->macSz;
        ssh->peerAeadMode = ssh->handshake->aeadMode;
        WMEMCPY(&ssh->peerKeys, &ssh->handshake->peerKeys, sizeof(Keys));

        switch (ssh->peerEncryptId) {
            case ID_NONE:
                WLOG(WS_LOG_DEBUG, "DNK: peer using cipher none");
                break;

#ifndef WOLFSSH_NO_AES_CBC
            case ID_AES128_CBC:
            case ID_AES192_CBC:
            case ID_AES256_CBC:
                WLOG(WS_LOG_DEBUG, "DNK: peer using cipher aes-cbc");
                ret = wc_AesSetKey(&ssh->decryptCipher.aes,
                                   ssh->peerKeys.encKey, ssh->peerKeys.encKeySz,
                                   ssh->peerKeys.iv, AES_DECRYPTION);
                break;
#endif

#ifndef WOLFSSH_NO_AES_CTR
            case ID_AES128_CTR:
            case ID_AES192_CTR:
            case ID_AES256_CTR:
                WLOG(WS_LOG_DEBUG, "DNK: peer using cipher aes-ctr");
                ret = wc_AesSetKey(&ssh->decryptCipher.aes,
                                   ssh->peerKeys.encKey, ssh->peerKeys.encKeySz,
                                   ssh->peerKeys.iv, AES_ENCRYPTION);
                break;
#endif

#ifndef WOLFSSH_NO_AES_GCM
            case ID_AES128_GCM:
            case ID_AES192_GCM:
            case ID_AES256_GCM:
                WLOG(WS_LOG_DEBUG, "DNK: peer using cipher aes-gcm");
                ret = wc_AesGcmSetKey(&ssh->decryptCipher.aes,
                                      ssh->peerKeys.encKey,
                                      ssh->peerKeys.encKeySz);
                break;
#endif

            default:
                WLOG(WS_LOG_DEBUG, "DNK: peer using cipher invalid");
                break;
        }

        if (ret == 0)
            ret = WS_SUCCESS;
        else
            ret = WS_CRYPTO_FAILED;
    }

    if (ret == WS_SUCCESS) {
        ssh->rxCount = 0;
        ssh->highwaterFlag = 0;
        ssh->isKeying = 0;
        HandshakeInfoFree(ssh->handshake, ssh->ctx->heap);
        ssh->handshake = NULL;
        WLOG(WS_LOG_DEBUG, "Keying completed");
    }

    return ret;
}


#ifndef WOLFSSH_NO_DH_GEX_SHA256
static int DoKexDhGexRequest(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    word32 begin;
    int ret = WS_SUCCESS;

    if (ssh == NULL || ssh->handshake == NULL || buf == NULL || len == 0 ||
            idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetUint32(&ssh->handshake->dhGexMinSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        ret = GetUint32(&ssh->handshake->dhGexPreferredSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        ret = GetUint32(&ssh->handshake->dhGexMaxSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "  min = %u, preferred = %u, max = %u",
                ssh->handshake->dhGexMinSz,
                ssh->handshake->dhGexPreferredSz,
                ssh->handshake->dhGexMaxSz);
        *idx = begin;
        ret = SendKexDhGexGroup(ssh);
    }

    return ret;
}


static int DoKexDhGexGroup(WOLFSSH* ssh,
                           byte* buf, word32 len, word32* idx)
{
    const byte* primeGroup = NULL;
    word32 primeGroupSz;
    const byte* generator = NULL;
    word32 generatorSz;
    word32 begin;
    int ret = WS_SUCCESS;

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetMpint(&primeGroupSz, &primeGroup, buf, len, &begin);
        if (ret == WS_SUCCESS && primeGroupSz > (MAX_KEX_KEY_SZ + 1))
            ret = WS_DH_SIZE_E;
    }

    if (ret == WS_SUCCESS)
        ret = GetMpint(&generatorSz, &generator, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        if (ssh->handshake->primeGroup)
            WFREE(ssh->handshake->primeGroup, ssh->ctx->heap, DYNTYPE_MPINT);
        ssh->handshake->primeGroup =
            (byte*)WMALLOC(primeGroupSz, ssh->ctx->heap, DYNTYPE_MPINT);
        if (ssh->handshake->primeGroup == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        if (ssh->handshake->generator)
            WFREE(ssh->handshake->generator, ssh->ctx->heap, DYNTYPE_MPINT);
        ssh->handshake->generator =
            (byte*)WMALLOC(generatorSz, ssh->ctx->heap, DYNTYPE_MPINT);
        if (ssh->handshake->generator == NULL) {
            ret = WS_MEMORY_E;
            WFREE(ssh->handshake->primeGroup, ssh->ctx->heap, DYNTYPE_MPINT);
            ssh->handshake->primeGroup = NULL;
        }
    }

    if (ret == WS_SUCCESS) {
        WMEMCPY(ssh->handshake->primeGroup, primeGroup, primeGroupSz);
        ssh->handshake->primeGroupSz = primeGroupSz;
        WMEMCPY(ssh->handshake->generator, generator, generatorSz);
        ssh->handshake->generatorSz = generatorSz;

        *idx = begin;
        ret = SendKexDhInit(ssh);
    }

    return ret;
}
#endif


static int DoIgnore(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    word32 dataSz;
    word32 begin = *idx;

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(len);

    ato32(buf + begin, &dataSz);
    begin += LENGTH_SZ + dataSz;

    *idx = begin;

    return WS_SUCCESS;
}

static int DoRequestSuccess(WOLFSSH *ssh, byte *buf, word32 len, word32 *idx)
{
    word32 dataSz;
    word32 begin = *idx;
    int    ret=WS_SUCCESS;

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(len);

    WLOG(WS_LOG_DEBUG, "DoRequestSuccess, *idx=%d, len=%d", *idx, len);
    ato32(buf + begin, &dataSz);
    begin += LENGTH_SZ + dataSz;

    if (ssh->ctx->reqSuccessCb != NULL)
        ret = ssh->ctx->reqSuccessCb(ssh, &(buf[*idx]), len, ssh->reqSuccessCtx);

    *idx = begin;

    return ret;
}

static int DoRequestFailure(WOLFSSH *ssh, byte *buf, word32 len, word32 *idx)
{
    word32 dataSz;
    word32 begin = *idx;
    int ret = WS_SUCCESS;

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(len);

    WLOG(WS_LOG_DEBUG, "DoRequestFalure, *idx=%d, len=%d", *idx, len);
    ato32(buf + begin, &dataSz);
    begin += LENGTH_SZ + dataSz;

    if (ssh->ctx->reqFailureCb != NULL)
        ret = ssh->ctx->reqFailureCb(ssh, &(buf[*idx]), len, ssh->reqFailureCtx);

    *idx = begin;

    return ret;
}

static int DoDebug(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    byte  alwaysDisplay;
    char*    msg = NULL;
    char*    lang = NULL;
    word32 strSz;
    word32 begin;

    if (ssh == NULL || buf == NULL || idx == NULL ||
            len < (2 * LENGTH_SZ) + 1) {

        return WS_BAD_ARGUMENT;
    }
    begin = *idx;

    alwaysDisplay = buf[begin++];

    ato32(buf + begin, &strSz);
    begin += LENGTH_SZ;
    if (strSz > 0) {
        if (strSz > len - begin) {
            return WS_BUFFER_E;
        }

        msg = (char*)WMALLOC(strSz + 1, ssh->ctx->heap, DYNTYPE_STRING);
        if (msg != NULL) {
            WMEMCPY(msg, buf + begin, strSz);
            msg[strSz] = 0;
        }
        else {
            return WS_MEMORY_E;
        }
        begin += strSz;
    }

    if (LENGTH_SZ > len - begin) {
        WFREE(msg, ssh->ctx->heap, DYNTYPE_STRING);
        return WS_BUFFER_E;
    }

    ato32(buf + begin, &strSz);
    begin += LENGTH_SZ;
    if (strSz > 0) {
        if ((len < begin) || (strSz > len - begin)) {
            WFREE(msg, ssh->ctx->heap, DYNTYPE_STRING);
            return WS_BUFFER_E;
        }

        lang = (char*)WMALLOC(strSz + 1, ssh->ctx->heap, DYNTYPE_STRING);
        if (lang != NULL) {
            WMEMCPY(lang, buf + begin, strSz);
            lang[strSz] = 0;
        }
        else {
            WFREE(msg, ssh->ctx->heap, DYNTYPE_STRING);
            return WS_MEMORY_E;
        }
        begin += strSz;
    }

    if (alwaysDisplay) {
        WLOG(WS_LOG_DEBUG, "DEBUG MSG (%s): %s",
             (lang == NULL) ? "none" : lang,
             (msg == NULL) ? "no message" : msg);
    }

    *idx = begin;

    WFREE(msg, ssh->ctx->heap, DYNTYPE_STRING);
    WFREE(lang, ssh->ctx->heap, DYNTYPE_STRING);

    return WS_SUCCESS;
}


static int DoUnimplemented(WOLFSSH* ssh,
                           byte* buf, word32 len, word32* idx)
{
    word32 seq;
    word32 begin = *idx;

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(len);

    ato32(buf + begin, &seq);
    begin += UINT32_SZ;

    WLOG(WS_LOG_DEBUG, "UNIMPLEMENTED: seq %u", seq);

    *idx = begin;

    return WS_SUCCESS;
}


static int DoDisconnect(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    word32 reason;
    const char* reasonStr = NULL;
    word32 begin = *idx;

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(len);
    WOLFSSH_UNUSED(reasonStr);

    ato32(buf + begin, &reason);
    begin += UINT32_SZ;

#ifdef NO_WOLFSSH_STRINGS
    WLOG(WS_LOG_DEBUG, "DISCONNECT: (%u)", reason);
#elif defined(DEBUG_WOLFSSH)
    switch (reason) {
        case WOLFSSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT:
            reasonStr = "host not allowed to connect"; break;
        case WOLFSSH_DISCONNECT_PROTOCOL_ERROR:
            reasonStr = "protocol error"; break;
        case WOLFSSH_DISCONNECT_KEY_EXCHANGE_FAILED:
            reasonStr = "key exchange failed"; break;
        case WOLFSSH_DISCONNECT_RESERVED:
            reasonStr = "reserved"; break;
        case WOLFSSH_DISCONNECT_MAC_ERROR:
            reasonStr = "mac error"; break;
        case WOLFSSH_DISCONNECT_COMPRESSION_ERROR:
            reasonStr = "compression error"; break;
        case WOLFSSH_DISCONNECT_SERVICE_NOT_AVAILABLE:
            reasonStr = "service not available"; break;
        case WOLFSSH_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED:
            reasonStr = "protocol version not supported"; break;
        case WOLFSSH_DISCONNECT_HOST_KEY_NOT_VERIFIABLE:
            reasonStr = "host key not verifiable"; break;
        case WOLFSSH_DISCONNECT_CONNECTION_LOST:
            reasonStr = "connection lost"; break;
        case WOLFSSH_DISCONNECT_BY_APPLICATION:
            reasonStr = "disconnect by application"; break;
        case WOLFSSH_DISCONNECT_TOO_MANY_CONNECTIONS:
            reasonStr = "too many connections"; break;
        case WOLFSSH_DISCONNECT_AUTH_CANCELLED_BY_USER:
            reasonStr = "auth cancelled by user"; break;
        case WOLFSSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE:
            reasonStr = "no more auth methods available"; break;
        case WOLFSSH_DISCONNECT_ILLEGAL_USER_NAME:
            reasonStr = "illegal user name"; break;
        default:
            reasonStr = "unknown reason";
    }
    WLOG(WS_LOG_DEBUG, "DISCONNECT: (%u) %s", reason, reasonStr);
#endif

    *idx = begin;

    return WS_SUCCESS;
}


static int DoServiceRequest(WOLFSSH* ssh,
                            byte* buf, word32 len, word32* idx)
{
    word32 begin = *idx;
    word32 nameSz;
    char     serviceName[WOLFSSH_MAX_NAMESZ];

    WOLFSSH_UNUSED(len);

    ato32(buf + begin, &nameSz);
    begin += LENGTH_SZ;

    if (begin + nameSz > len || nameSz >= WOLFSSH_MAX_NAMESZ) {
        return WS_BUFFER_E;
    }

    WMEMCPY(serviceName, buf + begin, nameSz);
    begin += nameSz;
    serviceName[nameSz] = 0;

    *idx = begin;

    WLOG(WS_LOG_DEBUG, "Requesting service: %s", serviceName);
    ssh->clientState = CLIENT_USERAUTH_REQUEST_DONE;

    return WS_SUCCESS;
}


static int DoServiceAccept(WOLFSSH* ssh,
                           byte* buf, word32 len, word32* idx)
{
    word32 begin = *idx;
    word32 nameSz;
    char     serviceName[WOLFSSH_MAX_NAMESZ];

    WOLFSSH_UNUSED(len);

    ato32(buf + begin, &nameSz);
    begin += LENGTH_SZ;

    if (begin + nameSz > len || nameSz >= WOLFSSH_MAX_NAMESZ) {
        return WS_BUFFER_E;
    }

    WMEMCPY(serviceName, buf + begin, nameSz);
    begin += nameSz;
    serviceName[nameSz] = 0;

    *idx = begin;

    WLOG(WS_LOG_DEBUG, "Accepted service: %s", serviceName);
    ssh->serverState = SERVER_USERAUTH_REQUEST_DONE;

    return WS_SUCCESS;
}


#ifdef WOLFSSH_ALLOW_USERAUTH_NONE
/* Utility for DoUserAuthRequest() */
static int DoUserAuthRequestNone(WOLFSSH* ssh, WS_UserAuthData* authData,
                                     byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;
    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestNone()");

    WOLFSSH_UNUSED(len);

    if (ssh == NULL || authData == NULL ||
        buf == NULL || idx == NULL) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        authData->type = WOLFSSH_USERAUTH_NONE;
        if (ssh->ctx->userAuthCb != NULL) {
            WLOG(WS_LOG_DEBUG, "DUARN: Calling the userauth callback");
            ret = ssh->ctx->userAuthCb(WOLFSSH_USERAUTH_NONE,
                                       authData, ssh->userAuthCtx);
            if (ret == WOLFSSH_USERAUTH_SUCCESS) {
                WLOG(WS_LOG_DEBUG, "DUARN: none check successful");
                ssh->clientState = CLIENT_USERAUTH_DONE;
                ret = WS_SUCCESS;
            }
            else if (ret == WOLFSSH_USERAUTH_REJECTED) {
                WLOG(WS_LOG_DEBUG, "DUARN: password rejected");
                #ifndef NO_FAILURE_ON_REJECTED
                ret = SendUserAuthFailure(ssh, 0);
                if (ret == WS_SUCCESS)
                    ret = WS_USER_AUTH_E;
                #else
                ret = WS_USER_AUTH_E;
                #endif
            }
            else {
                WLOG(WS_LOG_DEBUG, "DUARN: none check failed, retry");
                ret = SendUserAuthFailure(ssh, 0);
            }
        }
        else {
            WLOG(WS_LOG_DEBUG, "DUARN: No user auth callback");
            ret = SendUserAuthFailure(ssh, 0);
            if (ret == WS_SUCCESS)
                ret = WS_FATAL_ERROR;
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestNone(), ret = %d", ret);
    return ret;
}
#endif


/* Utility for DoUserAuthRequest() */
static int DoUserAuthRequestPassword(WOLFSSH* ssh, WS_UserAuthData* authData,
                                     byte* buf, word32 len, word32* idx)
{
    word32 begin;
    WS_UserAuthData_Password* pw = NULL;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestPassword()");

    if (ssh == NULL || authData == NULL ||
        buf == NULL || len == 0 || idx == NULL) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        pw = &authData->sf.password;
        authData->type = WOLFSSH_USERAUTH_PASSWORD;
        ret = GetBoolean(&pw->hasNewPassword, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(&pw->passwordSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        pw->password = buf + begin;
        begin += pw->passwordSz;

        if (pw->hasNewPassword) {
            /* Skip the password change. Maybe error out since we aren't
             * supporting password changes at this time. */
            ret = GetUint32(&pw->newPasswordSz, buf, len, &begin);
            if (ret == WS_SUCCESS) {
                pw->newPassword = buf + begin;
                begin += pw->newPasswordSz;
            }
        }
        else {
            pw->newPassword = NULL;
            pw->newPasswordSz = 0;
        }

        if (ssh->ctx->userAuthCb != NULL) {
            WLOG(WS_LOG_DEBUG, "DUARPW: Calling the userauth callback");
            ret = ssh->ctx->userAuthCb(WOLFSSH_USERAUTH_PASSWORD,
                                       authData, ssh->userAuthCtx);
            if (ret == WOLFSSH_USERAUTH_SUCCESS) {
                WLOG(WS_LOG_DEBUG, "DUARPW: password check successful");
                ssh->clientState = CLIENT_USERAUTH_DONE;
                ret = WS_SUCCESS;
            }
            else if (ret == WOLFSSH_USERAUTH_REJECTED) {
                WLOG(WS_LOG_DEBUG, "DUARPW: password rejected");
                #ifndef NO_FAILURE_ON_REJECTED
                ret = SendUserAuthFailure(ssh, 0);
                if (ret == WS_SUCCESS)
                    ret = WS_USER_AUTH_E;
                #else
                ret = WS_USER_AUTH_E;
                #endif
            }
            else {
                WLOG(WS_LOG_DEBUG, "DUARPW: password check failed, retry");
                ret = SendUserAuthFailure(ssh, 0);
            }
        }
        else {
            WLOG(WS_LOG_DEBUG, "DUARPW: No user auth callback");
            ret = SendUserAuthFailure(ssh, 0);
            if (ret == WS_SUCCESS)
                ret = WS_FATAL_ERROR;
        }
    }

    if (ret == WS_SUCCESS)
        *idx = begin;

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestPassword(), ret = %d", ret);
    return ret;
}

#ifndef WOLFSSH_NO_RSA
/* Utility for DoUserAuthRequestPublicKey() */
/* returns negative for error, positive is size of digest. */
static int DoUserAuthRequestRsa(WOLFSSH* ssh, WS_UserAuthData_PublicKey* pk,
                                enum wc_HashType hashId, byte* digest,
                                word32 digestSz)
{
    enum wc_HashType enmhashId = hashId;
    byte *checkDigest = NULL;
    byte *encDigest = NULL;
    int checkDigestSz;
    const byte* publicKeyType;
    word32 publicKeyTypeSz = 0;
    const byte* n;
    word32 nSz = 0;
    const byte* e = NULL;
    word32 eSz = 0;
    word32 i = 0;
    int ret = WS_SUCCESS;
    RsaKey *key_ptr = NULL;

#ifndef WOLFSSH_SMALL_STACK
    byte s_checkDigest[MAX_ENCODED_SIG_SZ];
#endif

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestRsa()");

    if (ssh == NULL || ssh->ctx == NULL || pk == NULL || digest == NULL ||
            digestSz == 0) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
#ifdef WOLFSSH_SMALL_STACK
        checkDigest = (byte*)WMALLOC(MAX_ENCODED_SIG_SZ, ssh->ctx->heap,
                DYNTYPE_BUFFER);
        if (checkDigest == NULL)
            ret = WS_MEMORY_E;
#else
        checkDigest = s_checkDigest;
#endif
        key_ptr = (RsaKey*)WMALLOC(sizeof(RsaKey), ssh->ctx->heap,
                DYNTYPE_PUBKEY);
        if (key_ptr == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        WMEMSET(checkDigest, 0, MAX_ENCODED_SIG_SZ);
        ret = wc_InitRsaKey(key_ptr, ssh->ctx->heap);
        if (ret == 0) {
            ret = WS_SUCCESS;
        }
    }

    /* First check that the public key's type matches the one we are
     * expecting. */
    if (ret == WS_SUCCESS)
        ret = GetSize(&publicKeyTypeSz, pk->publicKey, pk->publicKeySz, &i);

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->publicKey + i;
        i += publicKeyTypeSz;
        if (publicKeyTypeSz != pk->publicKeyTypeSz &&
            WMEMCMP(publicKeyType, pk->publicKeyType, publicKeyTypeSz) != 0) {

            WLOG(WS_LOG_DEBUG,
                "Public Key's type does not match public key type");
            ret = WS_INVALID_ALGO_ID;
        }
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&eSz, pk->publicKey, pk->publicKeySz, &i);

    if (ret == WS_SUCCESS) {
        e = pk->publicKey + i;
        i += eSz;
        ret = GetSize(&nSz, pk->publicKey, pk->publicKeySz, &i);
    }

    if (ret == WS_SUCCESS) {
        n = pk->publicKey + i;

        ret = wc_RsaPublicKeyDecodeRaw(n, nSz, e, eSz, key_ptr);
        if (ret != 0) {
            WLOG(WS_LOG_DEBUG, "Could not decode public key");
            ret = WS_CRYPTO_FAILED;
        }
    }

    if (ret == WS_SUCCESS) {
        i = 0;
        /* First check that the signature's public key type matches the one
         * we are expecting. */
        ret = GetSize(&publicKeyTypeSz, pk->publicKey, pk->publicKeySz, &i);
    }

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->publicKey + i;
        i += publicKeyTypeSz;

        if (publicKeyTypeSz != pk->publicKeyTypeSz &&
            WMEMCMP(publicKeyType, pk->publicKeyType, publicKeyTypeSz) != 0) {

            WLOG(WS_LOG_DEBUG,
                 "Signature's type does not match public key type");
            ret = WS_INVALID_ALGO_ID;
        }
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&nSz, pk->signature, pk->signatureSz, &i);

    if (ret == WS_SUCCESS) {
        n = pk->signature + i;
        checkDigestSz = wc_RsaSSL_Verify(n, nSz, checkDigest,
                                         MAX_ENCODED_SIG_SZ, key_ptr);
        if (checkDigestSz <= 0) {
            WLOG(WS_LOG_DEBUG, "Could not verify signature");
            ret = WS_CRYPTO_FAILED;
        }
    }

    if (ret == WS_SUCCESS) {
        word32 encDigestSz;
        volatile int compare;
        volatile int sizeCompare;
#ifdef WOLFSSH_SMALL_STACK
        encDigest = (byte*)WMALLOC(MAX_ENCODED_SIG_SZ, ssh->ctx->heap,
                DYNTYPE_BUFFER);
        if (encDigest == NULL)
            ret = WS_MEMORY_E;

        if (ret == WS_SUCCESS)
#else
        byte s_encDigest[MAX_ENCODED_SIG_SZ];
        encDigest = s_encDigest;
#endif
        {
            WMEMSET(encDigest, 0, MAX_ENCODED_SIG_SZ);
            encDigestSz = wc_EncodeSignature(encDigest, digest,
                                         wc_HashGetDigestSize(enmhashId),
                                         wc_HashGetOID(enmhashId));

            compare = ConstantCompare(encDigest, checkDigest,
                                  encDigestSz);
            sizeCompare = encDigestSz != (word32)checkDigestSz;

            if ((compare == 0) && (sizeCompare == 0))
                ret = WS_SUCCESS;
            else
                ret = WS_RSA_E;
        }
    }
    if (key_ptr != NULL) {
        wc_FreeRsaKey(key_ptr);
        WFREE(key_ptr, ssh->ctx->heap, DYNTYPE_PUBKEY);
    }
#ifdef WOLFSSH_SMALL_STACK
    if (checkDigest)
        WFREE(checkDigest, ssh->ctx->heap, DYNTYPE_BUFFER);
    if (encDigest)
        WFREE(encDigest, ssh->ctx_heap, DYNTYPE_BUFFER);
#endif
    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestRsa(), ret = %d", ret);
    return ret;
}


#ifdef WOLFSSH_CERTS
/* return WS_SUCCESS on success */
static int DoUserAuthRequestRsaCert(WOLFSSH* ssh, WS_UserAuthData_PublicKey* pk,
                                    enum wc_HashType hashId, byte* digest,
                                    word32 digestSz)
{
    enum wc_HashType enmhashId = hashId;
    byte *checkDigest = NULL;
    byte *encDigest = NULL;
    int checkDigestSz;
    const byte* publicKeyType;
    word32 publicKeyTypeSz = 0;
    const byte* sig;
    word32 sigSz = 0;
    word32 i = 0;
    int ret = WS_SUCCESS;
    RsaKey *key_ptr = NULL;

#ifndef WOLFSSH_SMALL_STACK
    byte s_checkDigest[MAX_ENCODED_SIG_SZ];
    RsaKey s_key;
#endif

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestRsaCert()");

    if (ssh == NULL || ssh->ctx == NULL || pk == NULL || digest == NULL ||
            digestSz == 0) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
#ifdef WOLFSSH_SMALL_STACK
    checkDigest = (byte*)WMALLOC(MAX_ENCODED_SIG_SZ, ssh->ctx->heap,
            DYNTYPE_BUFFER);
    key_ptr = (RsaKey*)WMALLOC(sizeof(RsaKey), ssh->ctx->heap,
            DYNTYPE_PUBKEY);
    if (checkDigest == NULL || key_ptr == NULL)
        ret = WS_MEMORY_E;
#else
    checkDigest = s_checkDigest;
    key_ptr = &s_key;
#endif
    }

    if (ret == WS_SUCCESS) {
        ret = wc_InitRsaKey(key_ptr, ssh->ctx->heap);
        if (ret == 0) {
            ret = WS_SUCCESS;
        }
    }

    if (ret == WS_SUCCESS) {
        byte*  pub = NULL;
        word32 pubSz;
        DecodedCert cert;

        wc_InitDecodedCert(&cert, pk->publicKey, pk->publicKeySz,
                ssh->ctx->heap);
        ret = wc_ParseCert(&cert, CA_TYPE, 0, NULL);
        if (ret == 0) {
            ret = wc_GetPubKeyDerFromCert(&cert, NULL, &pubSz);
            if (ret == LENGTH_ONLY_E) {
                pub = WMALLOC(pubSz, ssh->ctx->heap, DYNTYPE_PUBKEY);
                if (pub == NULL) {
                    ret = WS_MEMORY_E;
                }
                else {
                    ret = wc_GetPubKeyDerFromCert(&cert, pub, &pubSz);
                }
            }
        }

        if (ret == 0) {
            word32 idx = 0;
            ret = wc_RsaPublicKeyDecode(pub, &idx, key_ptr, pubSz);
        }

        if (pub != NULL)
            WFREE(pub, ssh->ctx->heap, DYNTYPE_PUBKEY);
        wc_FreeDecodedCert(&cert);
    }

    if (ret != 0) {
        WLOG(WS_LOG_DEBUG, "Could not decode public key");
        ret = WS_CRYPTO_FAILED;
    }

    if (ret == WS_SUCCESS) {
        int keySz = wc_RsaEncryptSize(key_ptr) * 8;
        if (keySz < 2048) {
            WLOG(WS_LOG_DEBUG, "Key size too small (%d)", keySz);
            ret = WS_CERT_KEY_SIZE_E;
        }
    }

    if (ret == WS_SUCCESS) {
        i = 0;
        /* First check that the signature's public key type matches the one
         * we are expecting. */
        ret = GetSize(&publicKeyTypeSz, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->signature + i;
        i += publicKeyTypeSz;
        WOLFSSH_UNUSED(publicKeyType);
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&sigSz, pk->signature, pk->signatureSz, &i);

    if (ret == WS_SUCCESS) {
        sig = pk->signature + i;
        checkDigestSz = wc_RsaSSL_Verify(sig, sigSz, checkDigest,
                                         MAX_ENCODED_SIG_SZ, key_ptr);
        if (checkDigestSz <= 0) {
            WLOG(WS_LOG_DEBUG, "Could not verify signature");
            ret = WS_CRYPTO_FAILED;
        }
    }

    if (ret == WS_SUCCESS) {
        word32 encDigestSz;
        volatile int compare;
        volatile int sizeCompare;
#ifdef WOLFSSH_SMALL_STACK
        encDigest = (byte*)WMALLOC(MAX_ENCODED_SIG_SZ, ssh->ctx->heap,
                DYNTYPE_BUFFER);
        if (encDigest == NULL)
            ret = WS_MEMORY_E;

        if (ret == WS_SUCCESS)
#else
        byte s_encDigest[MAX_ENCODED_SIG_SZ];
        encDigest = s_encDigest;
#endif
        {
            encDigestSz = wc_EncodeSignature(encDigest, digest,
                                         wc_HashGetDigestSize(enmhashId),
                                         wc_HashGetOID(enmhashId));

            compare = ConstantCompare(encDigest, checkDigest,
                                  encDigestSz);
            sizeCompare = encDigestSz != (word32)checkDigestSz;

            if ((compare == 0) && (sizeCompare == 0))
                ret = WS_SUCCESS;
            else
                ret = WS_RSA_E;
        }
    }

    if (key_ptr)
        wc_FreeRsaKey(key_ptr);
#ifdef WOLFSSH_SMALL_STACK
    if (key_ptr)
        WFREE(key_ptr, ssh->ctx->heap, DYNTYPE_PUBKEY);
    if (checkDigest)
        WFREE(checkDigest, ssh->ctx->heap, DYNTYPE_BUFFER);
#endif
    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestRsaCert(), ret = %d", ret);
    return ret;
}
#endif /* WOLFSSH_CERTS */
#endif /* ! WOLFSSH_NO_RSA */


#ifndef WOLFSSH_NO_ECDSA

#define ECDSA_ASN_SIG_SZ 256

/* Utility for DoUserAuthRequestPublicKey() */
/* returns negative for error, positive is size of digest. */
static int DoUserAuthRequestEcc(WOLFSSH* ssh, WS_UserAuthData_PublicKey* pk,
                                enum wc_HashType hashId, byte* digest,
                                word32 digestSz)
{
    const byte* publicKeyType;
    word32 publicKeyTypeSz = 0;
    const byte* curveName;
    word32 curveNameSz = 0;
    const byte* q = NULL;
    const byte* r;
    const byte* s;
    word32 sz, qSz, rSz, sSz;
    word32 i = 0, asnSigSz = ECDSA_ASN_SIG_SZ;
    int ret = WS_SUCCESS;
    ecc_key *key_ptr = NULL;
    byte* asnSig = NULL;
#ifndef WOLFSSH_SMALL_STACK
    ecc_key s_key;
    byte s_asnSig[ECDSA_ASN_SIG_SZ];
#endif

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestEcc()");

    if (ssh == NULL || ssh->ctx == NULL || pk == NULL || digest == NULL ||
            digestSz == 0) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
    #ifdef WOLFSSH_SMALL_STACK
        key_ptr = (ecc_key*)WMALLOC(sizeof(ecc_key), ssh->ctx->heap,
                DYNTYPE_PUBKEY);
        asnSig = (byte*)WMALLOC(asnSigSz, ssh->ctx->heap, DYNTYPE_STRING);
        if (key_ptr == NULL || asnSig == NULL)
            ret = WS_MEMORY_E;
    #else
        key_ptr = &s_key;
        asnSig = s_asnSig;
    #endif
    }

    if (ret == WS_SUCCESS) {
        if (wc_ecc_init_ex(key_ptr, ssh->ctx->heap, INVALID_DEVID) != 0) {
            ret = WS_MEMORY_E;
        }
    }

    /* First check that the public key's type matches the one we are
     * expecting. */
    if (ret == WS_SUCCESS)
        ret = GetSize(&publicKeyTypeSz, pk->publicKey, pk->publicKeySz, &i);

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->publicKey + i;
        i += publicKeyTypeSz;
        if (publicKeyTypeSz != pk->publicKeyTypeSz &&
            WMEMCMP(publicKeyType, pk->publicKeyType, publicKeyTypeSz) != 0) {

            WLOG(WS_LOG_DEBUG,
                "Public Key's type does not match public key type");
            ret = WS_INVALID_ALGO_ID;
        }
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&curveNameSz, pk->publicKey, pk->publicKeySz, &i);

    if (ret == WS_SUCCESS) {
        curveName = pk->publicKey + i;
        WOLFSSH_UNUSED(curveName);
            /* Not used at the moment, hush the compiler. */
        i += curveNameSz;
        ret = GetSize(&qSz, pk->publicKey, pk->publicKeySz, &i);
    }

    if (ret == WS_SUCCESS) {
        q = pk->publicKey + i;
        i += qSz;
        ret = wc_ecc_import_x963(q, qSz, key_ptr);
    }

    if (ret != 0) {
        WLOG(WS_LOG_DEBUG, "Could not decode public key");
        ret = WS_CRYPTO_FAILED;
    }

    if (ret == WS_SUCCESS) {
        i = 0;
        /* First check that the signature's public key type matches the one
         * we are expecting. */
        ret = GetSize(&publicKeyTypeSz, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->signature + i;
        i += publicKeyTypeSz;

        if (publicKeyTypeSz != pk->publicKeyTypeSz &&
            WMEMCMP(publicKeyType, pk->publicKeyType, publicKeyTypeSz) != 0) {

            WLOG(WS_LOG_DEBUG,
                 "Signature's type does not match public key type");
            ret = WS_INVALID_ALGO_ID;
        }
    }

    if (ret == WS_SUCCESS) {
        /* Get the size of the signature blob. */
        ret = GetSize(&sz, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = GetStringRef(&rSz, &r, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = GetStringRef(&sSz, &s, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = wc_ecc_rs_raw_to_sig(r, rSz, s, sSz,
                asnSig, &asnSigSz);
        if (ret == 0) {
            ret = WS_SUCCESS;
        }
        else {
            WLOG(WS_LOG_DEBUG,
                "DUARE: ECC RS raw to sig fail (%d)",
                ret);
            ret = WS_ECC_E;
        }
    }

    if (ret == WS_SUCCESS) {
        ret = wc_SignatureVerifyHash(
                         hashId,
                         WC_SIGNATURE_TYPE_ECC,
                         digest, digestSz, asnSig, asnSigSz,
                         key_ptr, sizeof *key_ptr);
        if (ret != 0) {
            WLOG(WS_LOG_DEBUG,
                "DUARE: Signature Verify fail (%d)",
                ret);
            ret = WS_ECC_E;
        }
        else {
            ret = WS_SUCCESS;
        }
    }

    if (key_ptr)
        wc_ecc_free(key_ptr);
#ifdef WOLFSSH_SMALL_STACK
    if (asnSig)
        WFREE(asnSig, ssh->ctx->heap, DYNTYPE_STRING);
    if (key_ptr)
        WFREE(key_ptr, ssh->ctx->heap, DYNTYPE_PUBKEY);
#endif
    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestEcc(), ret = %d", ret);
    return ret;
}


#ifdef WOLFSSH_CERTS
static int DoUserAuthRequestEccCert(WOLFSSH* ssh, WS_UserAuthData_PublicKey* pk,
                                    enum wc_HashType hashId, byte* digest,
                                    word32 digestSz)
{
    const byte* publicKeyType;
    word32 publicKeyTypeSz = 0;
    const byte* r;
    const byte* s;
    word32 sz, rSz, sSz;
    word32 i = 0, asnSigSz = ECDSA_ASN_SIG_SZ;
    int ret = WS_SUCCESS;
    ecc_key *key_ptr = NULL;
    byte* asnSig = NULL;
#ifndef WOLFSSH_SMALL_STACK
    ecc_key s_key;
    byte s_asnSig[ECDSA_ASN_SIG_SZ];
#endif

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestEccCert()");

    if (ssh == NULL || ssh->ctx == NULL || pk == NULL || digest == NULL ||
            digestSz == 0) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
    #ifdef WOLFSSH_SMALL_STACK
        key_ptr = (ecc_key*)WMALLOC(sizeof(ecc_key), ssh->ctx->heap,
                DYNTYPE_PUBKEY);
        asnSigSz = ECDSA_ASN_SIG_SZ;
        asnSig = (byte*)WMALLOC(asnSigSz, ssh->ctx->heap, DYNTYPE_STRING);
        if (key_ptr == NULL || asnSig == NULL)
            ret = WS_MEMORY_E;
    #else
        key_ptr = &s_key;
        asnSig = s_asnSig;
    #endif
    }

    if (ret == WS_SUCCESS) {
        if (wc_ecc_init_ex(key_ptr, ssh->ctx->heap, INVALID_DEVID) != 0) {
            ret = WS_MEMORY_E;
        }
    }

    if (ret == WS_SUCCESS) {
        byte*  pub = NULL;
        word32 pubSz;
        DecodedCert cert;

        wc_InitDecodedCert(&cert, pk->publicKey, pk->publicKeySz,
                ssh->ctx->heap);
        ret = wc_ParseCert(&cert, CA_TYPE, 0, NULL);
        if (ret == 0) {
            ret = wc_GetPubKeyDerFromCert(&cert, NULL, &pubSz);
            if (ret == LENGTH_ONLY_E) {
                pub = WMALLOC(pubSz, ssh->ctx->heap, DYNTYPE_PUBKEY);
                if (pub == NULL) {
                    ret = WS_MEMORY_E;
                }
                else {
                    ret = wc_GetPubKeyDerFromCert(&cert, pub, &pubSz);
                }
            }
        }

        if (ret == 0) {
            word32 idx = 0;
            ret = wc_EccPublicKeyDecode(pub, &idx, key_ptr, pubSz);
        }

        if (pub != NULL)
            WFREE(pub, ssh->ctx->heap, DYNTYPE_PUBKEY);
        wc_FreeDecodedCert(&cert);
    }

    if (ret != 0) {
        WLOG(WS_LOG_DEBUG, "Could not decode public key");
        ret = WS_CRYPTO_FAILED;
    }

    if (ret == WS_SUCCESS) {
        i = 0;
        /* First check that the signature's public key type matches the one
         * we are expecting. */
        ret = GetSize(&publicKeyTypeSz, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        publicKeyType = pk->signature + i;
        i += publicKeyTypeSz;
        WOLFSSH_UNUSED(publicKeyType);
    }

    if (ret == WS_SUCCESS) {
        /* Get the size of the signature blob. */
        ret = GetSize(&sz, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = GetStringRef(&rSz, &r, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = GetStringRef(&sSz, &s, pk->signature, pk->signatureSz, &i);
    }

    if (ret == WS_SUCCESS) {
        ret = wc_ecc_rs_raw_to_sig(r, rSz, s, sSz,
                asnSig, &asnSigSz);
        if (ret == 0) {
            ret = WS_SUCCESS;
        }
        else {
            WLOG(WS_LOG_DEBUG,
                "DUAREC: ECC RS raw to sig fail (%d)",
                ret);
            ret = WS_ECC_E;
        }
    }

    if (ret == WS_SUCCESS) {
        ret = wc_SignatureVerifyHash(
                         hashId,
                         WC_SIGNATURE_TYPE_ECC,
                         digest, digestSz, asnSig, asnSigSz,
                         key_ptr, sizeof *key_ptr);
        if (ret != 0) {
            WLOG(WS_LOG_DEBUG,
                "DUAREC: Signature Verify fail (%d)",
                ret);
            ret = WS_ECC_E;
        }
        else {
            ret = WS_SUCCESS;
        }
    }

    if (key_ptr)
        wc_ecc_free(key_ptr);
#ifdef WOLFSSH_SMALL_STACK
    if (asnSig)
        WFREE(asnSig, ssh->ctx->heap, DYNTYPE_STRING);
    if (key_ptr)
        WFREE(key_ptr, ssh->ctx->heap, DYNTYPE_PUBKEY);
#endif
    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestEccCert(), ret = %d", ret);
    return ret;
}
#endif /* WOLFSSH_CERTS */
#endif /* ! WOLFSSH_NO_ECDSA */


#if !defined(WOLFSSH_NO_RSA) || !defined(WOLFSSH_NO_ECDSA)
/* Utility for DoUserAuthRequest() */
static int DoUserAuthRequestPublicKey(WOLFSSH* ssh, WS_UserAuthData* authData,
                                      byte* buf, word32 len, word32* idx)
{
    word32 begin;
    WS_UserAuthData_PublicKey* pk = NULL;
    int ret = WS_SUCCESS;
    int authFailure = 0;
    byte pkTypeId = ID_NONE;
    byte*  pkOk   = NULL;
    word32 pkOkSz = 0;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequestPublicKey()");

    if (ssh == NULL || authData == NULL ||
        buf == NULL || len == 0 || idx == NULL) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        pk = &authData->sf.publicKey;
        authData->type = WOLFSSH_USERAUTH_PUBLICKEY;
        ret = GetBoolean(&pk->hasSignature, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&pk->publicKeyTypeSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        pk->publicKeyType = buf + begin;
        begin += pk->publicKeyTypeSz;

        pkTypeId = NameToId((char*)pk->publicKeyType, pk->publicKeyTypeSz);
        if (pkTypeId == ID_UNKNOWN) {
            WLOG(WS_LOG_DEBUG, "DUARPK: Unknown / Unsupported key type");
            ret = SendUserAuthFailure(ssh, 0);
            authFailure = 1;
        }
    }

    if (ret == WS_SUCCESS && !authFailure)
        ret = GetSize(&pk->publicKeySz, buf, len, &begin);

    if (ret == WS_SUCCESS && !authFailure) {
        pk->publicKey = buf + begin;
        begin += pk->publicKeySz;

        /* store response values if public key is ok */
        pkOk   = (byte*)pk->publicKey;
        pkOkSz = pk->publicKeySz;

        #ifdef WOLFSSH_CERTS
        if (pkTypeId == ID_X509V3_SSH_RSA ||
            pkTypeId == ID_X509V3_ECDSA_SHA2_NISTP256 ||
            pkTypeId == ID_X509V3_ECDSA_SHA2_NISTP384 ||
            pkTypeId == ID_X509V3_ECDSA_SHA2_NISTP521) {
            byte  *cert   = NULL;
            word32 certSz = 0;

            ret = ParseAndVerifyCert(ssh, (byte*)pk->publicKey, pk->publicKeySz,
                &cert, &certSz);
            if (ret == WS_SUCCESS) {
                pk->isCert      = 1;
                pk->publicKey   = cert;
                pk->publicKeySz = certSz;
            }
            else {
                WLOG(WS_LOG_DEBUG, "DUARPK: client cert not verified");
                ret = SendUserAuthFailure(ssh, 0);
                authFailure = 1;
            }
        }
        #endif /* WOLFSSH_CERTS */

        if (ret == WS_SUCCESS && !authFailure) {
            if (pk->hasSignature) {
                ret = GetSize(&pk->signatureSz, buf, len, &begin);
                if (ret == WS_SUCCESS) {
                    pk->signature = buf + begin;
                    begin += pk->signatureSz;
                }
            }
            else {
                pk->signature = NULL;
                pk->signatureSz = 0;
            }
        }

        if (ret == WS_SUCCESS && !authFailure) {
            *idx = begin;

            if (ssh->ctx->userAuthCb != NULL) {
                WLOG(WS_LOG_DEBUG, "DUARPK: Calling the userauth callback");
                ret = ssh->ctx->userAuthCb(WOLFSSH_USERAUTH_PUBLICKEY,
                                           authData, ssh->userAuthCtx);
                WLOG(WS_LOG_DEBUG, "DUARPK: callback result = %d", ret);
                if (ret == WOLFSSH_USERAUTH_SUCCESS)
                    ret = WS_SUCCESS;
                else if (ret == WOLFSSH_USERAUTH_INVALID_PUBLICKEY) {
                    WLOG(WS_LOG_DEBUG, "DUARPK: client key rejected");
                    ret = SendUserAuthFailure(ssh, 0);
                    authFailure = 1;
                }
                else {
                    ret = SendUserAuthFailure(ssh, 0);
                    authFailure = 1;
                }
            }
            else {
                WLOG(WS_LOG_DEBUG, "DUARPK: no userauth callback set");
                ret = SendUserAuthFailure(ssh, 0);
                authFailure = 1;
            }
        }
    }

    if (ret == WS_SUCCESS && !authFailure) {
        if (pk->signature == NULL) {
            WLOG(WS_LOG_DEBUG, "DUARPK: Send the PK OK");
            ret = SendUserAuthPkOk(ssh, pk->publicKeyType, pk->publicKeyTypeSz,
                                    pkOk, pkOkSz);
        }
        else {
            wc_HashAlg hash;
            byte digest[WC_MAX_DIGEST_SIZE];
            word32 digestSz = 0;
            enum wc_HashType hashId = WC_HASH_TYPE_SHA;

            if (ret == WS_SUCCESS) {
                hashId = HashForId(pkTypeId);
                WMEMSET(digest, 0, sizeof(digest));
                ret = wc_HashGetDigestSize(hashId);
                if (ret > 0) {
                    digestSz = ret;
                    ret = 0;
                }
            }

            if (ret == 0)
                ret = wc_HashInit(&hash, hashId);

            if (ret == 0) {
                c32toa(ssh->sessionIdSz, digest);
                ret = HashUpdate(&hash, hashId, digest, UINT32_SZ);
            }

            if (ret == 0)
                ret = HashUpdate(&hash, hashId,
                                    ssh->sessionId, ssh->sessionIdSz);

            if (ret == 0) {
                digest[0] = MSGID_USERAUTH_REQUEST;
                ret = HashUpdate(&hash, hashId, digest, MSG_ID_SZ);
            }

            /* The rest of the fields in the signature are already
             * in the buffer. Just need to account for the sizes, which total
             * the length of the buffer minus the signature and uint32 size of
             * signature. */
            if (ret == 0) {
                word32 dataToSignSz;

                dataToSignSz = len - pk->signatureSz - UINT32_SZ;
                ret = HashUpdate(&hash, hashId,
                        pk->dataToSign, dataToSignSz);
            }
            if (ret == 0) {
                ret = wc_HashFinal(&hash, hashId, digest);

                if (ret != 0)
                    ret = WS_CRYPTO_FAILED;
                else
                    ret = WS_SUCCESS;
            }
            wc_HashFree(&hash, hashId);

            if (ret == WS_SUCCESS) {
                switch (pkTypeId) {
                    #ifndef WOLFSSH_NO_RSA
                    case ID_SSH_RSA:
                        ret = DoUserAuthRequestRsa(ssh, pk,
                                hashId, digest, digestSz);
                        break;
                    #ifdef WOLFSSH_CERTS
                    case ID_X509V3_SSH_RSA:
                        ret = DoUserAuthRequestRsaCert(ssh, pk,
                                hashId, digest, digestSz);
                        break;
                    #endif
                    #endif
                    #ifndef WOLFSSH_NO_ECDSA
                    case ID_ECDSA_SHA2_NISTP256:
                    case ID_ECDSA_SHA2_NISTP384:
                    case ID_ECDSA_SHA2_NISTP521:
                        ret = DoUserAuthRequestEcc(ssh, pk,
                                hashId, digest, digestSz);
                        break;
                    #ifdef WOLFSSH_CERTS
                    case ID_X509V3_ECDSA_SHA2_NISTP256:
                    case ID_X509V3_ECDSA_SHA2_NISTP384:
                    case ID_X509V3_ECDSA_SHA2_NISTP521:
                        ret = DoUserAuthRequestEccCert(ssh, pk,
                                hashId, digest, digestSz);
                        break;
                    #endif
                    #endif
                    default:
                        ret = WS_INVALID_ALGO_ID;
                }
            }

            if (ret != WS_SUCCESS) {
                if (ssh->ctx->userAuthResultCb) {
                    ssh->ctx->userAuthResultCb(WOLFSSH_USERAUTH_FAILURE,
                            authData, ssh->userAuthResultCtx);
                }
                WLOG(WS_LOG_DEBUG, "DUARPK: signature compare failure : [%d]",
                        ret);
                ret = SendUserAuthFailure(ssh, 0);
            }
            else {
                if (ssh->ctx->userAuthResultCb) {
                    if (ssh->ctx->userAuthResultCb(WOLFSSH_USERAUTH_SUCCESS,
                            authData, ssh->userAuthResultCtx) != WS_SUCCESS) {

                        WLOG(WS_LOG_DEBUG, "DUARPK: user overriding success");
                        ret = SendUserAuthFailure(ssh, 0);
                    }
                    else {
                        ssh->clientState = CLIENT_USERAUTH_DONE;
                    }
                }
                else {
                    ssh->clientState = CLIENT_USERAUTH_DONE;
                }
            }
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequestPublicKey(), ret = %d", ret);
    return ret;
}
#endif


static int DoUserAuthRequest(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    word32 begin;
    int ret = WS_SUCCESS;
    byte authNameId;
    WS_UserAuthData authData;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthRequest()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        WMEMSET(&authData, 0, sizeof(authData));
        ret = GetSize(&authData.usernameSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        authData.username = buf + begin;
        begin += authData.usernameSz;

        ret = GetUint32(&authData.serviceNameSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        if (authData.serviceNameSz > len - begin) {
            ret = WS_BUFFER_E;
        }
    }

    if (ret == WS_SUCCESS) {
        authData.serviceName = buf + begin;
        begin += authData.serviceNameSz;

        ret = GetSize(&authData.authNameSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        authData.authName = buf + begin;
        begin += authData.authNameSz;
        authNameId = NameToId((char*)authData.authName, authData.authNameSz);

        if (authNameId == ID_USERAUTH_PASSWORD)
            ret = DoUserAuthRequestPassword(ssh, &authData, buf, len, &begin);
#if !defined(WOLFSSH_NO_RSA) || !defined(WOLFSSH_NO_ECDSA)
        else if (authNameId == ID_USERAUTH_PUBLICKEY) {
            authData.sf.publicKey.dataToSign = buf + *idx;
            ret = DoUserAuthRequestPublicKey(ssh, &authData, buf, len, &begin);
        }
#endif
#ifdef WOLFSSH_ALLOW_USERAUTH_NONE
        else if (authNameId == ID_NONE) {
            ret = DoUserAuthRequestNone(ssh, &authData, buf, len, &begin);
        }
#endif
        else {
            WLOG(WS_LOG_DEBUG,
                 "invalid userauth type: %s", IdToName(authNameId));
            ret = SendUserAuthFailure(ssh, 0);
        }

        if (ret == WS_SUCCESS) {
            ret = wolfSSH_SetUsernameRaw(ssh,
                    authData.username, authData.usernameSz);
        }

        *idx = begin;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthRequest(), ret = %d", ret);
    return ret;
}


static int DoUserAuthFailure(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    byte authList[3]; /* Should only ever be password, publickey, hostname */
    word32 authListSz = 3;
    byte partialSuccess;
    byte authType = 0;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthFailure()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = GetNameList(authList, &authListSz, buf, len, idx);

    if (ret == WS_SUCCESS)
        ret = GetBoolean(&partialSuccess, buf, len, idx);

    if (ret == WS_SUCCESS) {
        word32 i;

        /* check authList to see if authId is there */
        for (i = 0; i < authListSz; i++) {
            word32 j;
            for (j = 0; j < sizeof(ssh->supportedAuth); j++) {
                if (authList[i] == ssh->supportedAuth[j]) {
                    switch(authList[i]) {
                        case ID_USERAUTH_PASSWORD:
                            authType |= WOLFSSH_USERAUTH_PASSWORD;
                            break;
#if !defined(WOLFSSH_NO_RSA) || !defined(WOLFSSH_NO_ECDSA)
                        case ID_USERAUTH_PUBLICKEY:
                            authType |= WOLFSSH_USERAUTH_PUBLICKEY;
                            break;
#endif
                        default:
                            break;
                    }
                }
            }
        }

        /* the auth type attempted was not in the list */
        if (authType == 0) {
            WLOG(WS_LOG_DEBUG, "Did not match any auth IDs in peers list");
            ret = WS_USER_AUTH_E;
        }
    }

    if (ret == WS_SUCCESS) {
        ret = SendUserAuthRequest(ssh, authType, 0);
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthFailure(), ret = %d", ret);
    return ret;
}


static int DoUserAuthSuccess(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthSuccess()");

    /* This message does not have any payload. len should be 0. */
    if (ssh == NULL || buf == NULL || len != 0 || idx == NULL) {
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthSuccess(), ret = %d", ret);
        return ret;
    }

    ssh->serverState = SERVER_USERAUTH_ACCEPT_DONE;

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthSuccess(), ret = %d", ret);
    return ret;
}


static int DoUserAuthBanner(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    char banner[80];
    word32 bannerSz = sizeof(banner);
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoUserAuthBanner()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = GetString(banner, &bannerSz, buf, len, idx);

    if (ret == WS_SUCCESS)
        ret = GetSize(&bannerSz, buf, len, idx);

    if (ret == WS_SUCCESS) {
        if (ssh->ctx->showBanner) {
            WLOG(WS_LOG_INFO, "%s", banner);
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoUserAuthBanner(), ret = %d", ret);
    return ret;
}


#ifdef WOLFSSH_FWD
static int DoGlobalRequestFwd(WOLFSSH* ssh,
        byte* buf, word32 len, word32* idx, int wantReply, int isCancel)
{
    word32 begin;
    int ret = WS_SUCCESS;
    char* bindAddr = NULL;
    word32 bindPort;

    WLOG(WS_LOG_DEBUG, "Entering DoGlobalRequestFwd()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL) {
        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        WLOG(WS_LOG_INFO, "wantReply = %d, isCancel = %d", wantReply, isCancel);
        ret = GetStringAlloc(ssh->ctx->heap, &bindAddr, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        ret = GetUint32(&bindPort, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "Requesting forwarding%s for address %s on port %u.",
                isCancel ? " cancel" : "", bindAddr, bindPort);
    }

    if (ret == WS_SUCCESS && wantReply) {
        ret = SendGlobalRequestFwdSuccess(ssh, 1, bindPort);
    }

    if (ret == WS_SUCCESS) {
        if (ssh->ctx->fwdCb) {
            ret = ssh->ctx->fwdCb(isCancel ? WOLFSSH_FWD_REMOTE_CLEANUP :
                        WOLFSSH_FWD_REMOTE_SETUP,
                    ssh->fwdCbCtx, bindAddr, bindPort);
        }
    }

    if (bindAddr != NULL)
        WFREE(bindAddr, ssh->ctx->heap, DYNTYPE_STRING);

    WLOG(WS_LOG_DEBUG, "Leaving DoGlobalRequestFwd(), ret = %d", ret);
    return ret;
}
#endif

static int DoGlobalRequest(WOLFSSH* ssh,
                           byte* buf, word32 len, word32* idx)
{
    word32 begin;
    int ret = WS_SUCCESS;
    char name[80];
    word32 nameSz = sizeof(name);
    int globReqId = ID_UNKNOWN;
    byte wantReply = 0;

    WLOG(WS_LOG_DEBUG, "Entering DoGlobalRequest()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL) {
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving DoGlobalRequest(), ret = %d", ret);
        return ret;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetString(name, &nameSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "DGR: request name = %s", name);
        globReqId = NameToId(name, nameSz);
        ret = GetBoolean(&wantReply, buf, len, &begin);
    }

    if (ret == WS_SUCCESS) {
        switch (globReqId) {
#ifdef WOLFSSH_FWD
            case ID_GLOBREQ_TCPIP_FWD:
                ret = DoGlobalRequestFwd(ssh, buf, len, &begin, wantReply, 0);
                wantReply = 0;
                break;
            case ID_GLOBREQ_TCPIP_FWD_CANCEL:
                ret = DoGlobalRequestFwd(ssh, buf, len, &begin, wantReply, 1);
                wantReply = 0;
                break;
#endif
            default:
                if (ssh->ctx->globalReqCb != NULL) {
                    ret = ssh->ctx->globalReqCb(ssh, name, nameSz, wantReply,
                            (void *)ssh->globalReqCtx);

                    if (wantReply) {
                        ret = SendRequestSuccess(ssh, (ret == WS_SUCCESS));
                    }
                }
                else if (wantReply)
                    ret = SendRequestSuccess(ssh, 0);
                    /* response SSH_MSG_REQUEST_FAILURE to Keep-Alive.
                     * IETF:draft-ssh-global-requests */
                break;
        }
    }

    if (ret == WS_SUCCESS) {
        *idx += len;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoGlobalRequest(), ret = %d", ret);
    return ret;
}


#ifdef WOLFSSH_FWD
static int DoChannelOpenForward(WOLFSSH* ssh,
                         char** host, word32* hostPort,
                         char** origin, word32* originPort,
                         byte* buf, word32 len, word32* idx)
{
    word32 begin;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelOpenForward()");

    if (idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetStringAlloc(ssh->ctx->heap, host, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(hostPort, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetStringAlloc(ssh->ctx->heap, origin, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetUint32(originPort, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        *idx = begin;
        WLOG(WS_LOG_INFO, "  host = %s:%u", *host, *hostPort);
        WLOG(WS_LOG_INFO, "  origin = %s:%u", *origin, *originPort);
    }
    else {
        WFREE(*host, ssh->ctx->heap, DYNTYPE_STRING);
        WFREE(*origin, ssh->ctx->heap, DYNTYPE_STRING);
        *host = NULL;
        *origin = NULL;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelOpenForward(), ret = %d", ret);
    return ret;
}
#endif /* WOLFSSH_FWD */


static int DoChannelOpen(WOLFSSH* ssh,
                         byte* buf, word32 len, word32* idx)
{
    word32 begin;
    word32 typeSz;
    char type[32];
    byte typeId = ID_UNKNOWN;
    word32 peerChannelId;
    word32 peerInitialWindowSz;
    word32 peerMaxPacketSz;
#ifdef WOLFSSH_FWD
    char* host = NULL;
    char* origin = NULL;
    word32 hostPort = 0, originPort = 0;
    int isDirect = 0;
#endif /* WOLFSSH_FWD */
    WOLFSSH_CHANNEL* newChannel = NULL;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelOpen()");

    if (idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        typeSz = sizeof(type);
        ret = GetString(type, &typeSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerChannelId, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerInitialWindowSz, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerMaxPacketSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "  type = %s", type);
        WLOG(WS_LOG_INFO, "  peerChannelId = %u", peerChannelId);
        WLOG(WS_LOG_INFO, "  peerInitialWindowSz = %u", peerInitialWindowSz);
        WLOG(WS_LOG_INFO, "  peerMaxPacketSz = %u", peerMaxPacketSz);

        typeId = NameToId(type, typeSz);
        switch (typeId) {
            case ID_CHANTYPE_SESSION:
                break;
        #ifdef WOLFSSH_FWD
            case ID_CHANTYPE_TCPIP_DIRECT:
                isDirect = 1;
                NO_BREAK;
            case ID_CHANTYPE_TCPIP_FORWARD:
                ret = DoChannelOpenForward(ssh,
                                &host, &hostPort, &origin, &originPort,
                                buf, len, &begin);
                break;
        #endif /* WOLFSSH_FWD */
        #ifdef WOLFSSH_AGENT
            case ID_CHANTYPE_AUTH_AGENT:
                WLOG(WS_LOG_INFO, "agent = %p", ssh->agent);
                if (ssh->agent != NULL)
                    ssh->agent->channel = peerChannelId;
                else
                    ret = WS_AGENT_NULL_E;
                break;
        #endif
            default:
                ret = WS_INVALID_CHANTYPE;
        }
    }

    if (ret == WS_SUCCESS) {
        *idx = begin;

        newChannel = ChannelNew(ssh, typeId,
                                ssh->ctx->windowSz, ssh->ctx->maxPacketSz);
        if (newChannel == NULL)
            ret = WS_RESOURCE_E;
        else {
            ChannelUpdatePeer(newChannel, peerChannelId,
                          peerInitialWindowSz, peerMaxPacketSz);
            if (ssh->channelListSz == 0)
                ssh->defaultPeerChannelId = peerChannelId;
        #ifdef WOLFSSH_FWD
            if (typeId == ID_CHANTYPE_TCPIP_DIRECT) {
                ChannelUpdateForward(newChannel,
                        host, hostPort, origin, originPort, isDirect);

                if (ssh->ctx->fwdCb) {
                    ret = ssh->ctx->fwdCb(WOLFSSH_FWD_LOCAL_SETUP,
                            ssh->fwdCbCtx, host, hostPort);
                    if (ret == WS_SUCCESS) {
                        ret = ssh->ctx->fwdCb(WOLFSSH_FWD_CHANNEL_ID,
                                ssh->fwdCbCtx, NULL, newChannel->channel);
                    }
                }
            }
        #endif /* WOLFSSH_FWD */
            ChannelAppend(ssh, newChannel);

            ssh->clientState = CLIENT_CHANNEL_OPEN_DONE;
        }
    }

    if (ret == WS_SUCCESS)
        ret = SendChannelOpenConf(ssh, newChannel);

#ifdef WOLFSSH_FWD
    /* ChannelUpdateForward makes new host and origin buffer */
    WFREE(host, ssh->ctx->heap, DYNTYPE_STRING);
    WFREE(origin, ssh->ctx->heap, DYNTYPE_STRING);
#endif /* WOLFSSH_FWD */

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelOpen(), ret = %d", ret);
    return ret;
}


static int DoChannelOpenConf(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel;
    word32 begin, channelId, peerChannelId,
           peerInitialWindowSz, peerMaxPacketSz;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelOpenConf()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetUint32(&channelId, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerChannelId, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerInitialWindowSz, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetUint32(&peerMaxPacketSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "  channelId = %u", channelId);
        WLOG(WS_LOG_INFO, "  peerChannelId = %u", peerChannelId);
        WLOG(WS_LOG_INFO, "  peerInitialWindowSz = %u", peerInitialWindowSz);
        WLOG(WS_LOG_INFO, "  peerMaxPacketSz = %u", peerMaxPacketSz);

        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS)
        ret = ChannelUpdatePeer(channel, peerChannelId,
                            peerInitialWindowSz, peerMaxPacketSz);

    if (ret == WS_SUCCESS) {
        ssh->serverState = SERVER_CHANNEL_OPEN_DONE;
        ssh->defaultPeerChannelId = peerChannelId;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelOpenConf(), ret = %d", ret);
    return ret;
}


static int DoChannelOpenFail(WOLFSSH* ssh,
                             byte* buf, word32 len, word32* idx)
{
    char desc[80];
    word32 begin, channelId, reasonId, descSz, langSz;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelOpenFail()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        ret = GetUint32(&channelId, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetUint32(&reasonId, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        descSz = sizeof(desc);
        ret = GetString(desc, &descSz, buf, len, &begin);
    }

    if (ret == WS_SUCCESS)
        ret = GetSize(&langSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        *idx = begin + langSz;

        WLOG(WS_LOG_INFO, "channel open failure reason code: %u", reasonId);
        if (descSz > 0) {
            WLOG(WS_LOG_INFO, "description: %s", desc);
        }

        ret = ChannelRemove(ssh, channelId, WS_CHANNEL_ID_SELF);
    }

    if (ret == WS_SUCCESS)
        ret = WS_CHANOPEN_FAILED;

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelOpenFail(), ret = %d", ret);
    return ret;
}


static int DoChannelEof(WOLFSSH* ssh,
                        byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 channelId;
    int      ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelEof()");

    ret = GetUint32(&channelId, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        *idx = begin;

        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        channel->eofRxd = 1;
        if (!channel->eofTxd) {
            ret = SendChannelEof(ssh, channel->peerChannel);
        }
        ssh->lastRxId = channelId;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelEof(), ret = %d", ret);
    return ret;
}


static int DoChannelClose(WOLFSSH* ssh,
                          byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 channelId;
    int ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelClose()");

    ret = GetUint32(&channelId, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        *idx = begin;

        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        if (!channel->closeTxd) {
            ret = SendChannelClose(ssh, channel->peerChannel);
        }
    }

    if (ret == WS_SUCCESS) {
        ret = ChannelRemove(ssh, channelId, WS_CHANNEL_ID_SELF);
    }

    if (ret == WS_SUCCESS) {
        ret = WS_CHANNEL_CLOSED;
        ssh->lastRxId = channelId;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelClose(), ret = %d", ret);
    return ret;
}


static int DoChannelRequest(WOLFSSH* ssh,
                            byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 channelId;
    word32 typeSz;
    char type[32];
    byte wantReply;
    int ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelRequest()");

    ret = GetUint32(&channelId, buf, len, &begin);

    typeSz = sizeof(type);
    if (ret == WS_SUCCESS)
        ret = GetString(type, &typeSz, buf, len, &begin);

    if (ret == WS_SUCCESS)
        ret = GetBoolean(&wantReply, buf, len, &begin);

    if (ret != WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "Leaving DoChannelRequest(), ret = %d", ret);
        return ret;
    }

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "  channelId = %u", channelId);
        WLOG(WS_LOG_DEBUG, "  type = %s", type);
        WLOG(WS_LOG_DEBUG, "  wantReply = %u", wantReply);

#ifdef WOLFSSH_TERM
        if (WSTRNCMP(type, "pty-req", typeSz) == 0) {
            char term[32];
            word32 termSz;
            word32 widthChar, heightRows, widthPixels, heightPixels;
            word32 modesSz;

            termSz = sizeof(term);
            ret = GetString(term, &termSz, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetUint32(&widthChar, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetUint32(&heightRows, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetUint32(&widthPixels, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetUint32(&heightPixels, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetUint32(&modesSz, buf, len, &begin);

            if (ret == WS_SUCCESS) {
                WLOG(WS_LOG_DEBUG, "  term = %s", term);
                WLOG(WS_LOG_DEBUG, "  widthChar = %u", widthChar);
                WLOG(WS_LOG_DEBUG, "  heightRows = %u", heightRows);
                WLOG(WS_LOG_DEBUG, "  widthPixels = %u", widthPixels);
                WLOG(WS_LOG_DEBUG, "  heightPixels = %u", heightPixels);
                WLOG(WS_LOG_DEBUG, "  modes = %u", (modesSz - 1) / 5);
            }
        }
        else
#endif /* WOLFSSH_TERM */
        if (WSTRNCMP(type, "env", typeSz) == 0) {
            char name[WOLFSSH_MAX_NAMESZ];
            word32 nameSz;
            char value[32];
            word32 valueSz;

            name[0] = 0;
            value[0] = 0;
            nameSz = sizeof(name);
            valueSz = sizeof(value);
            ret = GetString(name, &nameSz, buf, len, &begin);
            if (ret == WS_SUCCESS)
                ret = GetString(value, &valueSz, buf, len, &begin);

            WLOG(WS_LOG_DEBUG, "  %s = %s", name, value);
        }
        else if (WSTRNCMP(type, "shell", typeSz) == 0) {
            channel->sessionType = WOLFSSH_SESSION_SHELL;
            ssh->clientState = CLIENT_DONE;
        }
        else if (WSTRNCMP(type, "exec", typeSz) == 0) {
            ret = GetStringAlloc(ssh->ctx->heap, &channel->command,
                    buf, len, &begin);
            channel->sessionType = WOLFSSH_SESSION_EXEC;
            ssh->clientState = CLIENT_DONE;

            WLOG(WS_LOG_DEBUG, "  command = %s", channel->command);
        }
        else if (WSTRNCMP(type, "subsystem", typeSz) == 0) {
            ret = GetStringAlloc(ssh->ctx->heap, &channel->command,
                    buf, len, &begin);
            channel->sessionType = WOLFSSH_SESSION_SUBSYSTEM;
            ssh->clientState = CLIENT_DONE;

            WLOG(WS_LOG_DEBUG, "  subsystem = %s", channel->command);
        }
#ifdef WOLFSSH_AGENT
        else if (WSTRNCMP(type, "auth-agent-req@openssh.com", typeSz) == 0) {
            WLOG(WS_LOG_AGENT, "  ssh-agent");
            if (ssh->ctx->agentCb != NULL)
                ssh->useAgent = 1;
            else
                WLOG(WS_LOG_AGENT, "Agent callback not set, not using.");
        }
#endif /* WOLFSSH_AGENT */
    }

    if (ret == WS_SUCCESS)
        *idx = len;

    if (wantReply) {
        int replyRet;

        replyRet = SendChannelSuccess(ssh, channelId, (ret == WS_SUCCESS));
        if (replyRet != WS_SUCCESS)
            ret = replyRet;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelRequest(), ret = %d", ret);
    return ret;
}


static int DoChannelSuccess(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelSuccess()");

    if (ssh == NULL || buf == NULL || len == 0 || idx == NULL) {
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving DoChannelSuccess(), ret = %d", ret);
        return ret;
    }

    ssh->serverState = SERVER_DONE;

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelSuccess(), ret = %d", ret);
    return ret;
}


static int DoChannelFailure(WOLFSSH* ssh, byte* buf, word32 len, word32* idx)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelFailure()");

    if (ssh == NULL || buf == NULL || len != 0 || idx == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = WS_CHANOPEN_FAILED;
    WLOG(WS_LOG_DEBUG, "Leaving DoChannelFailure(), ret = %d", ret);
    return ret;
}


static int DoChannelWindowAdjust(WOLFSSH* ssh,
                                 byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 channelId, bytesToAdd;
    int ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelWindowAdjust()");

    ret = GetUint32(&channelId, buf, len, &begin);
    if (ret == WS_SUCCESS)
        ret = GetUint32(&bytesToAdd, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        *idx = begin;

        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
        else {
            WLOG(WS_LOG_INFO, "  channelId = %u", channelId);
            WLOG(WS_LOG_INFO, "  bytesToAdd = %u", bytesToAdd);
            WLOG(WS_LOG_INFO, "  peerWindowSz = %u",
                 channel->peerWindowSz);

            channel->peerWindowSz += bytesToAdd;

            WLOG(WS_LOG_INFO, "  update peerWindowSz = %u",
                 channel->peerWindowSz);

        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelWindowAdjust(), ret = %d", ret);
    return ret;
}


static int DoChannelData(WOLFSSH* ssh,
                         byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 dataSz = 0;
    word32 channelId;
    int ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelData()");

    ret = GetUint32(&channelId, buf, len, &begin);
    if (ret == WS_SUCCESS)
        ret = GetSize(&dataSz, buf, len, &begin);

    /* Validate dataSz */
    if (ret == WS_SUCCESS) {
        if (len < begin) {
            ret = WS_RECV_OVERFLOW_E;
        }
    }

    if (ret == WS_SUCCESS) {
        *idx = begin + dataSz;

        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
        else
            ret = ChannelPutData(channel, buf + begin, dataSz);
    }

    if (ret == WS_SUCCESS) {
        ssh->lastRxId = channelId;
        ret = WS_CHAN_RXD;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelData(), ret = %d", ret);
    return ret;
}


/* deletes current buffer and updates it
 * return WS_SUCCESS on success */
static int PutBuffer(WOLFSSH_BUFFER* buf, byte* data, word32 dataSz)
{
    int ret;

    /* reset "used" section of buffer back to 0 */
    buf->length = 0;
    buf->idx    = 0;

    if (dataSz > buf->bufferSz) {
        if ((ret = GrowBuffer(buf, dataSz, 0)) != WS_SUCCESS) {
            return ret;
        }
    }
    WMEMCPY(buf->buffer, data, dataSz);
    buf->length = dataSz;

    return WS_SUCCESS;
}


static int DoChannelExtendedData(WOLFSSH* ssh,
                         byte* buf, word32 len, word32* idx)
{
    WOLFSSH_CHANNEL* channel = NULL;
    word32 begin = *idx;
    word32 dataSz = 0;
    word32 channelId;
    word32 dataTypeCode;
    int ret;

    WLOG(WS_LOG_DEBUG, "Entering DoChannelExtendedData()");

    ret = GetUint32(&channelId, buf, len, &begin);
    if (ret == WS_SUCCESS)
        ret = GetUint32(&dataTypeCode, buf, len, &begin);
    if (ret == WS_SUCCESS)
        ret = (dataTypeCode == CHANNEL_EXTENDED_DATA_STDERR) ?
            WS_SUCCESS : WS_INVALID_EXTDATA;
    if (ret == WS_SUCCESS)
        ret = GetSize(&dataSz, buf, len, &begin);

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
        else {
            ret = PutBuffer(&ssh->extDataBuffer,  buf + begin, dataSz);
            #ifdef DEBUG_WOLFSSH
            DumpOctetString(buf + begin, dataSz);
            #endif
            if (ret == WS_SUCCESS) {
                ret = SendChannelWindowAdjust(ssh, channel->channel, dataSz);
            }
        }
        *idx = begin + dataSz;
    }

    if (ret == WS_SUCCESS) {
        ssh->lastRxId = channelId;
        ret = WS_EXTDATA;
    }

    WLOG(WS_LOG_DEBUG, "Leaving DoChannelExtendedData(), ret = %d", ret);
    return ret;
}


static int DoPacket(WOLFSSH* ssh)
{
    byte* buf = (byte*)ssh->inputBuffer.buffer;
    word32 idx = ssh->inputBuffer.idx;
    word32 len = ssh->inputBuffer.length;
    word32 payloadSz;
    byte padSz;
    byte msg;
    word32 payloadIdx = 0;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "DoPacket sequence number: %d", ssh->peerSeq);

    idx += LENGTH_SZ;
    padSz = buf[idx++];

    /* check for underflow */
    if ((word32)(PAD_LENGTH_SZ + padSz + MSG_ID_SZ) > ssh->curSz) {
        return WS_OVERFLOW_E;
    }

    payloadSz = ssh->curSz - PAD_LENGTH_SZ - padSz - MSG_ID_SZ;

    msg = buf[idx++];
    /* At this point, payload starts at "buf + idx". */

    /* sanity check on payloadSz. Uses "or" condition because of the case when
     * adding idx to payloadSz causes it to overflow.
     */
    if ((ssh->inputBuffer.bufferSz < payloadSz + idx) ||
            (payloadSz + idx < payloadSz)) {
        return WS_OVERFLOW_E;
    }

    switch (msg) {

        case MSGID_DISCONNECT:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_DISCONNECT");
            ret = DoDisconnect(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_IGNORE:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_IGNORE");
            ret = DoIgnore(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_UNIMPLEMENTED:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_UNIMPLEMENTED");
            ret = DoUnimplemented(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_REQUEST_SUCCESS:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_REQUEST_SUCCESS");
            ret = DoRequestSuccess(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_REQUEST_FAILURE:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_REQUEST_FAILURE");
            ret = DoRequestFailure(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_DEBUG:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_DEBUG");
            ret = DoDebug(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_KEXINIT:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXINIT");
            ret = DoKexInit(ssh, buf + idx, payloadSz, &payloadIdx);
            if (ssh->isKeying == 1 &&
                    ssh->connectState == CONNECT_SERVER_CHANNEL_REQUEST_DONE) {
                if (ssh->handshake->kexId == ID_DH_GEX_SHA256) {
#if !defined(WOLFSSH_NO_DH) && !defined(WOLFSSH_NO_DH_GEX_SHA256)
                    ssh->error = SendKexDhGexRequest(ssh);
#endif
                }
                else
                    ssh->error = SendKexDhInit(ssh);
            }
            break;

        case MSGID_NEWKEYS:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_NEWKEYS");
            ret = DoNewKeys(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_KEXDH_INIT:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_INIT");
            ret = DoKexDhInit(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_KEXDH_REPLY:
            if (ssh->handshake == NULL) {
                ret = WS_MEMORY_E;
                break;
            }

            if (ssh->handshake->kexId == ID_DH_GEX_SHA256) {
#ifndef WOLFSSH_NO_DH_GEX_SHA256
                WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_GEX_GROUP");
                ret = DoKexDhGexGroup(ssh, buf + idx, payloadSz, &payloadIdx);
#endif
            }
            else {
                WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_REPLY");
                ret = DoKexDhReply(ssh, buf + idx, payloadSz, &payloadIdx);
            }
            break;

#ifndef WOLFSSH_NO_DH_GEX_SHA256
        case MSGID_KEXDH_GEX_REQUEST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_GEX_REQUEST");
            ret = DoKexDhGexRequest(ssh, buf + idx, payloadSz, &payloadIdx);
            break;
#endif

        case MSGID_KEXDH_GEX_INIT:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_GEX_INIT");
            ret = DoKexDhInit(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_KEXDH_GEX_REPLY:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_KEXDH_GEX_INIT");
            ret = DoKexDhReply(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_SERVICE_REQUEST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_SERVICE_REQUEST");
            ret = DoServiceRequest(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_SERVICE_ACCEPT:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_SERVER_ACCEPT");
            ret = DoServiceAccept(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_USERAUTH_REQUEST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_USERAUTH_REQUEST");
            ret = DoUserAuthRequest(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_USERAUTH_FAILURE:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_USERAUTH_FAILURE");
            ret = DoUserAuthFailure(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_USERAUTH_SUCCESS:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_USERAUTH_SUCCESS");
            ret = DoUserAuthSuccess(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_USERAUTH_BANNER:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_USERAUTH_BANNER");
            ret = DoUserAuthBanner(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_GLOBAL_REQUEST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_GLOBAL_REQUEST");
            ret = DoGlobalRequest(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_OPEN:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_OPEN");
            ret = DoChannelOpen(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_OPEN_CONF:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_OPEN_CONF");
            ret = DoChannelOpenConf(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_OPEN_FAIL:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_OPEN_FAIL");
            ret = DoChannelOpenFail(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_WINDOW_ADJUST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_WINDOW_ADJUST");
            ret = DoChannelWindowAdjust(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_DATA:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_DATA");
            ret = DoChannelData(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_EXTENDED_DATA:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_EXTENDED_DATA");
            ret = DoChannelExtendedData(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_EOF:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_EOF");
            ret = DoChannelEof(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_CLOSE:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_CLOSE");
            ret = DoChannelClose(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_REQUEST:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_REQUEST");
            ret = DoChannelRequest(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_SUCCESS:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_SUCCESS");
            ret = DoChannelSuccess(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        case MSGID_CHANNEL_FAILURE:
            WLOG(WS_LOG_DEBUG, "Decoding MSGID_CHANNEL_FAILURE");
            ret = DoChannelFailure(ssh, buf + idx, payloadSz, &payloadIdx);
            break;

        default:
            WLOG(WS_LOG_DEBUG, "Unimplemented message ID (%d)", msg);
#ifdef SHOW_UNIMPLEMENTED
            DumpOctetString(buf + idx, payloadSz);
#endif
            ret = SendUnimplemented(ssh);
    }

    if (payloadSz > 0) {
        idx += payloadIdx;
        if (idx + padSz > len) {
            WLOG(WS_LOG_DEBUG, "Not enough data in buffer for pad.");
            ret = WS_BUFFER_E;
        }
    }

    idx += padSz;
    ssh->inputBuffer.idx = idx;
    ssh->peerSeq++;

    return ret;
}


#ifndef WOLFSSH_NO_AES_CTR
#if defined(HAVE_FIPS) && defined(HAVE_FIPS_VERSION) && (HAVE_FIPS_VERSION == 2)
    /*
     * The FIPSv2 version of wc_AesCtrEncrypt() only works if the input and
     * output are different buffers. This helper copies each block into a
     * scratch buffer, then calling the AesCtrEncrypt() function on the
     * single scratch buffer. But, only in FIPS builds.
     */
    static INLINE int AesCtrEncryptHelper(Aes* aes,
        byte* out, const byte* in, word32 sz)
    {
        int ret = 0;
        byte scratch[AES_BLOCK_SIZE];

        if (aes == NULL || in == NULL || out == NULL || sz == 0)
            return WS_BAD_ARGUMENT;

        if (sz % AES_BLOCK_SIZE)
            return WS_ENCRYPT_E;

        while (ret == 0 && sz) {
            XMEMCPY(scratch, in, AES_BLOCK_SIZE);
            ret = wc_AesCtrEncrypt(aes, out, scratch, AES_BLOCK_SIZE);
            out += AES_BLOCK_SIZE;
            in += AES_BLOCK_SIZE;
            sz -= AES_BLOCK_SIZE;
        }
        ForceZero(scratch, sizeof(scratch));

        return ret;
    }
    #define AESCTRHELPER(a,b,c,d) AesCtrEncryptHelper((a),(b),(c),(d))
#else
    #define AESCTRHELPER(a,b,c,d) wc_AesCtrEncrypt((a),(b),(c),(d))
#endif
#endif


static INLINE int Encrypt(WOLFSSH* ssh, byte* cipher, const byte* input,
                          word16 sz)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || cipher == NULL || input == NULL || sz == 0)
        return WS_BAD_ARGUMENT;

    WLOG(WS_LOG_DEBUG, "Encrypt %s", IdToName(ssh->encryptId));

    switch (ssh->encryptId) {
        case ID_NONE:
            break;

#ifndef WOLFSSH_NO_AES_CBC
        case ID_AES128_CBC:
        case ID_AES192_CBC:
        case ID_AES256_CBC:
            if (sz % AES_BLOCK_SIZE || wc_AesCbcEncrypt(&ssh->encryptCipher.aes,
                                 cipher, input, sz) < 0) {

                ret = WS_ENCRYPT_E;
            }
            break;
#endif

#ifndef WOLFSSH_NO_AES_CTR
        case ID_AES128_CTR:
        case ID_AES192_CTR:
        case ID_AES256_CTR:
            if (sz % AES_BLOCK_SIZE || AESCTRHELPER(&ssh->encryptCipher.aes,
                                                       cipher, input, sz) < 0) {

                ret = WS_ENCRYPT_E;
            }
            break;
#endif

        default:
            ret = WS_INVALID_ALGO_ID;
    }

    ssh->txCount += sz;

    return ret;
}


static INLINE int Decrypt(WOLFSSH* ssh, byte* plain, const byte* input,
                          word16 sz)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || plain == NULL || input == NULL || sz == 0)
        return WS_BAD_ARGUMENT;

    WLOG(WS_LOG_DEBUG, "Decrypt %s", IdToName(ssh->peerEncryptId));

    switch (ssh->peerEncryptId) {
        case ID_NONE:
            break;

#ifndef WOLFSSH_NO_AES_CBC
        case ID_AES128_CBC:
        case ID_AES192_CBC:
        case ID_AES256_CBC:
            if (sz % AES_BLOCK_SIZE || wc_AesCbcDecrypt(&ssh->decryptCipher.aes,
                                 plain, input, sz) < 0) {

                ret = WS_DECRYPT_E;
            }
            break;
#endif

#ifndef WOLFSSH_NO_AES_CTR
        case ID_AES128_CTR:
        case ID_AES192_CTR:
        case ID_AES256_CTR:
            if (sz % AES_BLOCK_SIZE || AESCTRHELPER(&ssh->decryptCipher.aes,
                                                        plain, input, sz) < 0) {

                ret = WS_DECRYPT_E;
            }
            break;
#endif

        default:
            ret = WS_INVALID_ALGO_ID;
    }

    ssh->rxCount += sz;

    if (ret == WS_SUCCESS)
        ret = HighwaterCheck(ssh, WOLFSSH_HWSIDE_RECEIVE);

    return ret;
}


static INLINE int CreateMac(WOLFSSH* ssh, const byte* in, word32 inSz,
                            byte* mac)
{
    byte flatSeq[LENGTH_SZ];
    int ret;

    WMEMSET(flatSeq, 0, LENGTH_SZ);
    c32toa(ssh->seq, flatSeq);

    WLOG(WS_LOG_DEBUG, "CreateMac %s", IdToName(ssh->macId));

    /* Need to MAC the sequence number and the unencrypted packet */
    switch (ssh->macId) {
        case ID_NONE:
            ret = WS_SUCCESS;
            break;

#ifndef WOLFSSH_NO_HMAC_SHA1_96
        case ID_HMAC_SHA1_96:
            {
                Hmac hmac;
                byte digest[WC_SHA_DIGEST_SIZE];

                ret = wc_HmacInit(&hmac, ssh->ctx->heap, INVALID_DEVID);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacSetKey(&hmac, WC_SHA,
                                    ssh->keys.macKey, ssh->keys.macKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, flatSeq, sizeof(flatSeq));
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, in, inSz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacFinal(&hmac, digest);
                if (ret == WS_SUCCESS)
                    WMEMCPY(mac, digest, SHA1_96_SZ);
                wc_HmacFree(&hmac);
            }
            break;
#endif

#ifndef WOLFSSH_NO_HMAC_SHA1
        case ID_HMAC_SHA1:
            {
                Hmac hmac;

                ret = wc_HmacInit(&hmac, ssh->ctx->heap, INVALID_DEVID);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacSetKey(&hmac, WC_SHA,
                                    ssh->keys.macKey, ssh->keys.macKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, flatSeq, sizeof(flatSeq));
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, in, inSz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacFinal(&hmac, mac);
                wc_HmacFree(&hmac);
            }
            break;
#endif

        case ID_HMAC_SHA2_256:
            {
                Hmac hmac;

                ret = wc_HmacInit(&hmac, ssh->ctx->heap, INVALID_DEVID);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacSetKey(&hmac, WC_SHA256,
                                    ssh->keys.macKey,
                                    ssh->keys.macKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, flatSeq, sizeof(flatSeq));
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, in, inSz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacFinal(&hmac, mac);
                wc_HmacFree(&hmac);
            }
            break;

        default:
            WLOG(WS_LOG_DEBUG, "Invalid Mac ID");
            ret = WS_FATAL_ERROR;
    }

    return ret;
}


static INLINE int VerifyMac(WOLFSSH* ssh, const byte* in, word32 inSz,
                            const byte* mac)
{
    int ret;
    byte flatSeq[LENGTH_SZ];
    byte checkMac[MAX_HMAC_SZ];
    Hmac hmac;

    c32toa(ssh->peerSeq, flatSeq);

    WLOG(WS_LOG_DEBUG, "VerifyMac %s", IdToName(ssh->peerMacId));
    WLOG(WS_LOG_DEBUG, "VM: inSz = %u", inSz);
    WLOG(WS_LOG_DEBUG, "VM: seq = %u", ssh->peerSeq);
    WLOG(WS_LOG_DEBUG, "VM: keyLen = %u", ssh->peerKeys.macKeySz);

    WMEMSET(checkMac, 0, sizeof(checkMac));
    ret = wc_HmacInit(&hmac, ssh->ctx->heap, INVALID_DEVID);
    if (ret != WS_SUCCESS) {
        WLOG(WS_LOG_ERROR, "VM: Error initializing hmac structure");
    }
    else {
        switch (ssh->peerMacId) {
            case ID_NONE:
                ret = WS_SUCCESS;
                break;

            case ID_HMAC_SHA1:
            case ID_HMAC_SHA1_96:
                ret = wc_HmacSetKey(&hmac, WC_SHA, ssh->peerKeys.macKey,
                        ssh->peerKeys.macKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, flatSeq, sizeof(flatSeq));
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, in, inSz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacFinal(&hmac, checkMac);
                if (ConstantCompare(checkMac, mac, ssh->peerMacSz) != 0)
                    ret = WS_VERIFY_MAC_E;
                break;

            case ID_HMAC_SHA2_256:
                ret = wc_HmacSetKey(&hmac, WC_SHA256, ssh->peerKeys.macKey,
                        ssh->peerKeys.macKeySz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, flatSeq, sizeof(flatSeq));
                if (ret == WS_SUCCESS)
                    ret = wc_HmacUpdate(&hmac, in, inSz);
                if (ret == WS_SUCCESS)
                    ret = wc_HmacFinal(&hmac, checkMac);
                if (ConstantCompare(checkMac, mac, ssh->peerMacSz) != 0)
                    ret = WS_VERIFY_MAC_E;
                break;

            default:
                ret = WS_INVALID_ALGO_ID;
        }
        wc_HmacFree(&hmac);
    }

    return ret;
}


#ifndef WOLFSSH_NO_AEAD
static INLINE void AeadIncrementExpIv(byte* iv)
{
    int i;

    iv += AEAD_IMP_IV_SZ;

    for (i = AEAD_EXP_IV_SZ-1; i >= 0; i--) {
        if (++iv[i]) return;
    }
}


static INLINE int EncryptAead(WOLFSSH* ssh, byte* cipher,
                              const byte* input, word16 sz,
                              byte* authTag, const byte* auth,
                              word16 authSz)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || cipher == NULL || input == NULL || sz == 0 ||
        authTag == NULL || auth == NULL || authSz == 0)
        return WS_BAD_ARGUMENT;

    WLOG(WS_LOG_DEBUG, "EncryptAead %s", IdToName(ssh->encryptId));

    switch (ssh->encryptId) {
#ifndef WOLFSSH_NO_AES_GCM
        case ID_AES128_GCM:
        case ID_AES192_GCM:
        case ID_AES256_GCM:
            ret = wc_AesGcmEncrypt(&ssh->encryptCipher.aes, cipher, input, sz,
                    ssh->keys.iv, ssh->keys.ivSz,
                    authTag, ssh->macSz, auth, authSz);
            break;
#endif

        default:
            ret = WS_INVALID_ALGO_ID;
    }

    AeadIncrementExpIv(ssh->keys.iv);
    ssh->txCount += sz;

    return ret;
}


static INLINE int DecryptAead(WOLFSSH* ssh, byte* plain,
                              const byte* input, word16 sz,
                              const byte* authTag, const byte* auth,
                              word16 authSz)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || plain == NULL || input == NULL || sz == 0 ||
        authTag == NULL || auth == NULL || authSz == 0)
        return WS_BAD_ARGUMENT;

    WLOG(WS_LOG_DEBUG, "DecryptAead %s", IdToName(ssh->peerEncryptId));

    switch (ssh->peerEncryptId) {
#ifndef WOLFSSH_NO_AES_GCM
        case ID_AES128_GCM:
        case ID_AES192_GCM:
        case ID_AES256_GCM:
            ret = wc_AesGcmDecrypt(&ssh->decryptCipher.aes, plain, input, sz,
                    ssh->peerKeys.iv, ssh->peerKeys.ivSz,
                    authTag, ssh->peerMacSz, auth, authSz);
            break;
#endif

        default:
            ret = WS_INVALID_ALGO_ID;
    }

    AeadIncrementExpIv(ssh->peerKeys.iv);
    ssh->rxCount += sz;

    if (ret == WS_SUCCESS)
        ret = HighwaterCheck(ssh, WOLFSSH_HWSIDE_RECEIVE);

    return ret;
}
#endif /* WOLFSSH_NO_AEAD */


int DoReceive(WOLFSSH* ssh)
{
    int ret = WS_SUCCESS;
    int verifyResult;
    word32 readSz;
    byte peerBlockSz = ssh->peerBlockSz;
    byte peerMacSz = ssh->peerMacSz;
    byte aeadMode = ssh->peerAeadMode;

    switch (ssh->processReplyState) {
        case PROCESS_INIT:
            readSz = peerBlockSz;
            WLOG(WS_LOG_DEBUG, "PR1: size = %u", readSz);
            if ((ret = GetInputData(ssh, readSz)) < 0) {
                return ret;
            }
            ssh->processReplyState = PROCESS_PACKET_LENGTH;

            if (!aeadMode) {
                /* Decrypt first block if encrypted */
                ret = Decrypt(ssh,
                              ssh->inputBuffer.buffer +
                                 ssh->inputBuffer.idx,
                              ssh->inputBuffer.buffer +
                                 ssh->inputBuffer.idx,
                              readSz);
                if (ret != WS_SUCCESS) {
                    WLOG(WS_LOG_DEBUG, "PR: First decrypt fail");
                    ssh->error = ret;
                    return WS_FATAL_ERROR;
                }
            }
            NO_BREAK;

        case PROCESS_PACKET_LENGTH:
            if (ssh->inputBuffer.idx + UINT32_SZ >
                    ssh->inputBuffer.bufferSz) {
                ssh->error = WS_OVERFLOW_E;
                return WS_FATAL_ERROR;
            }

            /* Peek at the packet_length field. */
            ato32(ssh->inputBuffer.buffer + ssh->inputBuffer.idx,
                  &ssh->curSz);
            if (ssh->curSz >
                    MAX_PACKET_SZ - (word32)peerMacSz - LENGTH_SZ) {
                ssh->error = WS_OVERFLOW_E;
                return WS_FATAL_ERROR;
            }
            ssh->processReplyState = PROCESS_PACKET_FINISH;
            NO_BREAK;

        case PROCESS_PACKET_FINISH:
            readSz = ssh->curSz + LENGTH_SZ + peerMacSz;
            WLOG(WS_LOG_DEBUG, "PR2: size = %u", readSz);
            if (readSz > 0) {
                if ((ret = GetInputData(ssh, readSz)) < 0) {
                    return ret;
                }

                if (!aeadMode) {
                    if (ssh->curSz + LENGTH_SZ - peerBlockSz > 0) {
                        ret = Decrypt(ssh,
                                      ssh->inputBuffer.buffer +
                                         ssh->inputBuffer.idx + peerBlockSz,
                                      ssh->inputBuffer.buffer +
                                         ssh->inputBuffer.idx + peerBlockSz,
                                      ssh->curSz + LENGTH_SZ - peerBlockSz);
                    }
                    else {
                        /* Entire packet fit in one block, don't need
                         * to decrypt any more data this packet. */
                    }

                    /* Verify the buffer is big enough for the data and mac.
                     * Even if the decrypt step fails, verify the MAC anyway.
                     * This keeps consistent timing. */
                    verifyResult = VerifyMac(ssh,
                                             ssh->inputBuffer.buffer +
                                                 ssh->inputBuffer.idx,
                                             ssh->curSz + LENGTH_SZ,
                                             ssh->inputBuffer.buffer +
                                                 ssh->inputBuffer.idx +
                                                 LENGTH_SZ + ssh->curSz);
                    if (ret != WS_SUCCESS) {
                        WLOG(WS_LOG_DEBUG, "PR: Decrypt fail");
                        ssh->error = ret;
                        return WS_FATAL_ERROR;
                    }
                    if (verifyResult != WS_SUCCESS) {
                        WLOG(WS_LOG_DEBUG, "PR: VerifyMac fail");
                        ssh->error = verifyResult;
                        return WS_FATAL_ERROR;
                    }
                }
                else {
#ifndef WOLFSSH_NO_AEAD
                    ret = DecryptAead(ssh,
                                      ssh->inputBuffer.buffer +
                                         ssh->inputBuffer.idx +
                                         LENGTH_SZ,
                                      ssh->inputBuffer.buffer +
                                         ssh->inputBuffer.idx +
                                         LENGTH_SZ,
                                      ssh->curSz,
                                      ssh->inputBuffer.buffer +
                                          ssh->inputBuffer.idx +
                                          ssh->curSz + LENGTH_SZ,
                                      ssh->inputBuffer.buffer +
                                          ssh->inputBuffer.idx,
                                      LENGTH_SZ);

                    if (ret != WS_SUCCESS) {
                        WLOG(WS_LOG_DEBUG, "PR: DecryptAead fail");
                        ssh->error = ret;
                        return WS_FATAL_ERROR;
                    }
#endif
                }
            }
            ssh->processReplyState = PROCESS_PACKET;
            NO_BREAK;

        case PROCESS_PACKET:
            ret = DoPacket(ssh);
            ssh->error = ret;
            if (ret < 0 && !(ret == WS_CHAN_RXD || ret == WS_EXTDATA ||
                    ret == WS_CHANNEL_CLOSED || ret == WS_WANT_WRITE)) {
                return WS_FATAL_ERROR;
            }
            WLOG(WS_LOG_DEBUG, "PR3: peerMacSz = %u", peerMacSz);
            ssh->inputBuffer.idx += peerMacSz;
            break;

        default:
            WLOG(WS_LOG_DEBUG, "Bad process input state, program error");
            ssh->error = WS_INPUT_CASE_E;
            return WS_FATAL_ERROR;
    }
    WLOG(WS_LOG_DEBUG, "PR4: Shrinking input buffer");
    ShrinkBuffer(&ssh->inputBuffer, 1);
    ssh->processReplyState = PROCESS_INIT;

    WLOG(WS_LOG_DEBUG, "PR5: txCount = %u, rxCount = %u",
         ssh->txCount, ssh->rxCount);

    return ret;
}


int DoProtoId(WOLFSSH* ssh)
{
    int ret;
    word32 idSz;
    byte* eol;
    byte  SSH_PROTO_EOL_SZ = 1;

    if ( (ret = GetInputText(ssh, &eol)) < 0) {
        WLOG(WS_LOG_DEBUG, "get input text failed");
        return ret;
    }

    if (eol == NULL) {
        WLOG(WS_LOG_DEBUG, "invalid EOL");
        return WS_VERSION_E;
    }

    if (WSTRNCASECMP((char*)ssh->inputBuffer.buffer,
                     sshProtoIdStr, SSH_PROTO_SZ) == 0) {

        if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER)
            ssh->clientState = CLIENT_VERSION_DONE;
        else
            ssh->serverState = SERVER_VERSION_DONE;
    }
    else {
        WLOG(WS_LOG_DEBUG, "SSH version mismatch");
        return WS_VERSION_E;
    }
    if (WSTRNCMP((char*)ssh->inputBuffer.buffer,
                 OpenSSH, sizeof(OpenSSH)-1) == 0) {
        ssh->clientOpenSSH = 1;
    }

    if (*eol == '\r') {
        SSH_PROTO_EOL_SZ++;
    }
    *eol = 0;

    idSz = (word32)WSTRLEN((char*)ssh->inputBuffer.buffer);

    /* Store the proto ID for later use. It is used in keying and rekeying. */
    ssh->peerProtoId = (byte*)WMALLOC(idSz + LENGTH_SZ,
                                         ssh->ctx->heap, DYNTYPE_STRING);
    if (ssh->peerProtoId == NULL)
        ret = WS_MEMORY_E;
    else {
        c32toa(idSz, ssh->peerProtoId);
        WMEMCPY(ssh->peerProtoId + LENGTH_SZ, ssh->inputBuffer.buffer, idSz);
        ssh->peerProtoIdSz = idSz + LENGTH_SZ;
    }

    ssh->inputBuffer.idx += idSz + SSH_PROTO_EOL_SZ;
    ShrinkBuffer(&ssh->inputBuffer, 0);

    return ret;
}


int SendProtoId(WOLFSSH* ssh)
{
    int ret = WS_SUCCESS;
    word32 sshProtoIdStrSz;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_DEBUG, "%s", sshProtoIdStr);
        sshProtoIdStrSz = (word32)WSTRLEN(sshProtoIdStr);
        ret = GrowBuffer(&ssh->outputBuffer, sshProtoIdStrSz, 0);
    }

    if (ret == WS_SUCCESS) {
        WMEMCPY(ssh->outputBuffer.buffer, sshProtoIdStr, sshProtoIdStrSz);
        ssh->outputBuffer.length = sshProtoIdStrSz;
        ret = wolfSSH_SendPacket(ssh);
    }

    return ret;
}


/* payloadSz is an estimate. It should be a worst case. The actual value
 * will be nailed down when the packet is bundled to be sent. */
static int PreparePacket(WOLFSSH* ssh, word32 payloadSz)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        if (ssh->outputBuffer.length < ssh->outputBuffer.idx)
            ret = WS_OVERFLOW_E;
    }

    if (ret == WS_SUCCESS) {
        word32 packetSz, usedSz, outputSz;
        byte paddingSz;

        paddingSz = ssh->blockSz * 2;
        packetSz = PAD_LENGTH_SZ + payloadSz + paddingSz;
        outputSz = LENGTH_SZ + packetSz + ssh->macSz;
        usedSz = ssh->outputBuffer.length - ssh->outputBuffer.idx;

        ret = GrowBuffer(&ssh->outputBuffer, outputSz, usedSz);
    }

    if (ret == WS_SUCCESS) {
        ssh->packetStartIdx = ssh->outputBuffer.length;
        ssh->outputBuffer.length += LENGTH_SZ + PAD_LENGTH_SZ;
    }

    return ret;
}


static int BundlePacket(WOLFSSH* ssh)
{
    byte* output = NULL;
    word32 idx = 0;
    byte paddingSz = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        word32 payloadSz, packetSz;

        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        /* Calculate the actual payload size based on the data
         * written into the buffer. packetStartIdx is before the
         * LENGTH and PAD_LENGTH, subtract those out, as well. */
        payloadSz = idx - ssh->packetStartIdx - LENGTH_SZ - PAD_LENGTH_SZ;

        /* Minimum value for paddingSz is 4. */
        paddingSz = ssh->blockSz -
                    ((ssh->aeadMode ? 0 : LENGTH_SZ) +
                     PAD_LENGTH_SZ + payloadSz) % ssh->blockSz;
        if (paddingSz < MIN_PAD_LENGTH)
            paddingSz += ssh->blockSz;

        packetSz = PAD_LENGTH_SZ + payloadSz + paddingSz;

        /* fill in the packetSz, paddingSz */
        c32toa(packetSz, output + ssh->packetStartIdx);
        output[ssh->packetStartIdx + LENGTH_SZ] = paddingSz;

        /* end new */

        /* Add the padding */
        WLOG(WS_LOG_DEBUG, "BP: paddingSz = %u", paddingSz);
        if (ssh->encryptId == ID_NONE)
            WMEMSET(output + idx, 0, paddingSz);
        else if (wc_RNG_GenerateBlock(ssh->rng, output + idx, paddingSz) < 0) {
            ret = WS_CRYPTO_FAILED;
            WLOG(WS_LOG_DEBUG, "BP: failed to add padding");
        }
    }

    if (ret == WS_SUCCESS) {
        if (!ssh->aeadMode) {
            byte macSz = MacSzForId(ssh->macId);

            idx += paddingSz;

            WMEMSET(output + idx, 0, macSz);
            if (idx + macSz > ssh->outputBuffer.bufferSz) {
                ret = WS_BUFFER_E;
            }
            else {
                ret = CreateMac(ssh, ssh->outputBuffer.buffer +
                        ssh->packetStartIdx, ssh->outputBuffer.length -
                        ssh->packetStartIdx + paddingSz, output + idx);
            }

            if (ret == WS_SUCCESS) {
                idx += ssh->macSz;
                ret = Encrypt(ssh,
                              ssh->outputBuffer.buffer + ssh->packetStartIdx,
                              ssh->outputBuffer.buffer + ssh->packetStartIdx,
                              ssh->outputBuffer.length -
                                  ssh->packetStartIdx + paddingSz);
            }
            else {
                WLOG(WS_LOG_DEBUG, "BP: failed to generate mac");
            }
        }
        else {
#ifndef WOLFSSH_NO_AEAD
            idx += paddingSz;
            ret = EncryptAead(ssh,
                    ssh->outputBuffer.buffer + ssh->packetStartIdx + LENGTH_SZ,
                    ssh->outputBuffer.buffer + ssh->packetStartIdx + LENGTH_SZ,
                    ssh->outputBuffer.length - ssh->packetStartIdx + paddingSz
                        - LENGTH_SZ,
                    output + idx,
                    ssh->outputBuffer.buffer + ssh->packetStartIdx,
                    LENGTH_SZ);
            idx += ssh->macSz;
#else
            ret = WS_INVALID_ALGO_ID;
#endif
        }
    }

    if (ret == WS_SUCCESS) {
        ssh->seq++;
        ssh->outputBuffer.length = idx;
    }
    else {
        WLOG(WS_LOG_DEBUG, "BP: failed to encrypt buffer");
    }

    return ret;
}


static void PurgePacket(WOLFSSH* ssh)
{
    if (ssh != NULL) {
        ssh->packetStartIdx = 0;
        ssh->outputBuffer.idx = 0;
        ssh->outputBuffer.plainSz = 0;
        ShrinkBuffer(&ssh->outputBuffer, 1);
    }
}


static INLINE void CopyNameList(byte* buf, word32* idx,
                                                const char* src, word32 srcSz)
{
    word32 begin = *idx;

    c32toa(srcSz, buf + begin);
    begin += LENGTH_SZ;
    WMEMCPY(buf + begin, src, srcSz);
    begin += srcSz;

    *idx = begin;
}


/*
 * Iterates over a list of ID values and builds a string of names.
 *
 * @param buf       buffer to write names
 * @param bufSz     size of buffer to write names
 * @param src       source ID list
 * @param srcSz     size of the source ID list
 * @return          string length of buf after writing or WS_BUFFER_E
 */
static int BuildNameList(char* buf, word32 bufSz,
        const byte* src, word32 srcSz)
{
    const char* name;
    int nameSz, idx;

    WLOG(WS_LOG_DEBUG, "Entering BuildNameList()");
    idx = 0;
    do {
        name = IdToName(*src);
        nameSz = (int)WSTRLEN(name);

        WLOG(WS_LOG_DEBUG, "\tAdding name : %s", name);
        if (nameSz + 1 + idx > (int)bufSz) {
            idx = WS_BUFFER_E;
            break;
        }

        WMEMCPY(buf + idx, name, nameSz);
        idx += nameSz;

        src++;
        srcSz--;
        if (srcSz == 0) {
            buf[idx] = '\0';
        }
        else {
            buf[idx++] = ',';
        }
    } while (srcSz > 0);

    return idx;
}


static const char cannedEncAlgoNames[] =
#if !defined(WOLFSSH_NO_AES_GCM)
    "aes256-gcm@openssh.com,"
    "aes192-gcm@openssh.com,"
    "aes128-gcm@openssh.com,"
#endif
#if !defined(WOLFSSH_NO_AES_CTR)
    "aes256-ctr,"
    "aes192-ctr,"
    "aes128-ctr,"
#endif
#if !defined(WOLFSSH_NO_AES_CBC)
    "aes256-cbc,"
    "aes192-cbc,"
    "aes128-cbc,"
#endif
    "";
#if defined(WOLFSSH_NO_AES_GCM) && defined(WOLFSSH_NO_AES_CTR) && \
    defined(WOLFSSH_NO_AES_CBC)
#warning "You need at least one encryption algorithm."
#endif

static const char cannedMacAlgoNames[] =
#if !defined(WOLFSSH_NO_HMAC_SHA2_256)
    "hmac-sha2-256,"
#endif
#if !defined(WOLFSSH_NO_HMAC_SHA1_96)
    "hmac-sha1-96,"
#endif
#if !defined(WOLFSSH_NO_HMAC_SHA1)
    "hmac-sha1,"
#endif
    "";
#if defined(WOLFSSH_NO_HMAC_SHA2_256) && \
        defined(WOLFSSH_NO_HMAC_SHA1_96) && \
        defined(WOLFSSH_NO_HMAC_SHA1)
    #warning "You need at least one MAC algorithm."
#endif

#if defined(WOLFSSH_NO_ECDSA_SHA2_NISTP256) && \
        defined(WOLFSSH_NO_ECDSA_SHA2_NISTP384) && \
        defined(WOLFSSH_NO_ECDSA_SHA2_NISTP521) && \
        defined(WOLFSSH_NO_SSH_RSA_SHA1)
    #warning "You need at least one signing algorithm."
#endif

#define KEY_ALGO_SIZE_GUESS 28
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
    #ifdef WOLFSSH_CERTS
        static const char cannedKeyAlgoX509RsaNames[] = "x509v3-ssh-rsa";
    #endif
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    static const char cannedKeyAlgoEcc256Names[] = "ecdsa-sha2-nistp256";
    #ifdef WOLFSSH_CERTS
        static const char cannedKeyAlgoX509Ecc256Names[] =
                "x509v3-ecdsa-sha2-nistp256";
    #endif
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    static const char cannedKeyAlgoEcc384Names[] = "ecdsa-sha2-nistp384";
    #ifdef WOLFSSH_CERTS
        static const char cannedKeyAlgoX509Ecc384Names[] =
                "x509v3-ecdsa-sha2-nistp384";
    #endif
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    static const char cannedKeyAlgoEcc521Names[] = "ecdsa-sha2-nistp521";
    #ifdef WOLFSSH_CERTS
        static const char cannedKeyAlgoX509Ecc521Names[] =
                "x509v3-ecdsa-sha2-nistp521";
    #endif
#endif

static const char cannedKexAlgoNames[] =
#if !defined(WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256)
    "ecdh-sha2-nistp256-kyber-512-sha256,"
#endif
#if !defined(WOLFSSH_NO_ECDH_SHA2_NISTP521)
    "ecdh-sha2-nistp521,"
#endif
#if !defined(WOLFSSH_NO_ECDH_SHA2_NISTP384)
    "ecdh-sha2-nistp384,"
#endif
#if !defined(WOLFSSH_NO_ECDH_SHA2_NISTP256)
    "ecdh-sha2-nistp256,"
#endif
#if !defined(WOLFSSH_NO_DH_GEX_SHA256)
    "diffie-hellman-group-exchange-sha256,"
#endif
#if !defined(WOLFSSH_NO_DH_GROUP14_SHA1)
    "diffie-hellman-group14-sha1,"
#endif
#if !defined(WOLFSSH_NO_DH_GROUP1_SHA1)
    "diffie-hellman-group1-sha1,"
#endif
    "";

#if defined(WOLFSSH_NO_ECDH_SHA2_NISTP256) && \
        defined(WOLFSSH_NO_DH_GEX_SHA256) && \
        defined(WOLFSSH_NO_DH_GROUP14_SHA1) && \
        defined(WOLFSSH_NO_DH_GROUP1_SHA1) && \
        defined(WOLFSSH_NO_ECDH_SHA2_NISTP521) && \
        defined(WOLFSSH_NO_ECDH_SHA2_NISTP384) && \
        defined(WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256)
    #warning "You need at least one key exchange algorithm."
#endif

static const char cannedNoneNames[] = "none";

/* -1 for the null, some are -1 for the comma */
static const word32 cannedEncAlgoNamesSz = sizeof(cannedEncAlgoNames) - 2;
static const word32 cannedMacAlgoNamesSz = sizeof(cannedMacAlgoNames) - 2;
static const word32 cannedKexAlgoNamesSz = sizeof(cannedKexAlgoNames) - 2;
static const word32 cannedNoneNamesSz = sizeof(cannedNoneNames) - 1;

#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    static const word32 cannedKeyAlgoEcc256NamesSz =
            sizeof(cannedKeyAlgoEcc256Names) - 1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    static const word32 cannedKeyAlgoEcc384NamesSz =
            sizeof(cannedKeyAlgoEcc384Names) - 1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    static const word32 cannedKeyAlgoEcc521NamesSz =
            sizeof(cannedKeyAlgoEcc521Names) - 1;
#endif
#ifdef WOLFSSH_CERTS
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
    static const word32 cannedKeyAlgoX509Ecc256NamesSz =
            sizeof(cannedKeyAlgoX509Ecc256Names) - 1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
    static const word32 cannedKeyAlgoX509Ecc384NamesSz =
            sizeof(cannedKeyAlgoX509Ecc384Names) - 1;
#endif
#ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
    static const word32 cannedKeyAlgoX509Ecc521NamesSz =
            sizeof(cannedKeyAlgoX509Ecc521Names) - 1;
#endif
#endif /* WOLFSSH_CERTS */

int SendKexInit(WOLFSSH* ssh)
{
    byte* output = NULL;
    byte* payload = NULL;
    char* keyAlgoNames = NULL;
    const byte* privateKey;
    word32 idx = 0, payloadSz = 0, keyAlgoNamesSz = 0, privateKeyCount;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendKexInit()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER &&
            ssh->ctx->privateKeyCount == 0) {
        WLOG(WS_LOG_DEBUG, "Server needs at least one private key");
        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        ssh->isKeying = 1;
        if (ssh->handshake == NULL) {
            ssh->handshake = HandshakeInfoNew(ssh->ctx->heap);
            if (ssh->handshake == NULL) {
                WLOG(WS_LOG_DEBUG, "Couldn't allocate handshake info");
                ret = WS_MEMORY_E;
            }
        }
    }

    if (ret == WS_SUCCESS) {
        if (ssh->ctx->side == WOLFSSH_ENDPOINT_SERVER) {
            UpdateKeyID(ssh->ctx);
            privateKeyCount = ssh->ctx->privateKeyCount;
            privateKey = ssh->ctx->privateKeyId;
        }
        else {
            privateKeyCount = cannedKeyAlgoClientSz;
            privateKey = cannedKeyAlgoClient;
        }
        keyAlgoNamesSz = privateKeyCount * (KEY_ALGO_SIZE_GUESS + 1);
        keyAlgoNames = (char*)WMALLOC(keyAlgoNamesSz,
                ssh->ctx->heap, DYNTYPE_STRING);
        if (keyAlgoNames == NULL) {
            ret = WS_MEMORY_E;
        }
    }

    if (ret == WS_SUCCESS) {
        ret = BuildNameList(keyAlgoNames, keyAlgoNamesSz,
                privateKey, privateKeyCount);
        if (ret > 0) {
            keyAlgoNamesSz = (word32)ret;
            ret = WS_SUCCESS;
        }
    }

    if (ret == WS_SUCCESS) {
        payloadSz = MSG_ID_SZ + COOKIE_SZ + (LENGTH_SZ * 11) + BOOLEAN_SZ +
                   cannedKexAlgoNamesSz + keyAlgoNamesSz +
                   (cannedEncAlgoNamesSz * 2) +
                   (cannedMacAlgoNamesSz * 2) +
                   (cannedNoneNamesSz * 2);
        ret = PreparePacket(ssh, payloadSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;
        payload = output + idx;

        output[idx++] = MSGID_KEXINIT;

        ret = wc_RNG_GenerateBlock(ssh->rng, output + idx, COOKIE_SZ);
    }

    if (ret == WS_SUCCESS) {
        byte* buf;
        word32 bufSz = payloadSz + LENGTH_SZ;

        idx += COOKIE_SZ;

        CopyNameList(output, &idx, cannedKexAlgoNames, cannedKexAlgoNamesSz);
        CopyNameList(output, &idx, keyAlgoNames, keyAlgoNamesSz);
        CopyNameList(output, &idx, cannedEncAlgoNames, cannedEncAlgoNamesSz);
        CopyNameList(output, &idx, cannedEncAlgoNames, cannedEncAlgoNamesSz);
        CopyNameList(output, &idx, cannedMacAlgoNames, cannedMacAlgoNamesSz);
        CopyNameList(output, &idx, cannedMacAlgoNames, cannedMacAlgoNamesSz);
        CopyNameList(output, &idx, cannedNoneNames, cannedNoneNamesSz);
        CopyNameList(output, &idx, cannedNoneNames, cannedNoneNamesSz);
        c32toa(0, output + idx); /* Languages - Client To Server (0) */
        idx += LENGTH_SZ;
        c32toa(0, output + idx); /* Languages - Server To Client (0) */
        idx += LENGTH_SZ;
        output[idx++] = 0;       /* First KEX packet follows (false) */
        c32toa(0, output + idx); /* Reserved (0) */
        idx += LENGTH_SZ;

        if (ssh->handshake->kexInit != NULL) {
            WFREE(ssh->handshake->kexInit, ssh->ctx->heap, DYNTYPE_STRING);
            ssh->handshake->kexInit = NULL;
            ssh->handshake->kexInitSz = 0;
        }

        buf = (byte*)WMALLOC(bufSz, ssh->ctx->heap, DYNTYPE_STRING);
        if (buf == NULL) {
            WLOG(WS_LOG_DEBUG, "Cannot allocate storage for KEX Init msg");
            ret = WS_MEMORY_E;
        }
        else {
            c32toa(payloadSz, buf);
            WMEMCPY(buf + LENGTH_SZ, payload, payloadSz);
            ssh->handshake->kexInit = buf;
            ssh->handshake->kexInitSz = bufSz;
        }
    }

    WFREE(keyAlgoNames, ssh->ctx->heap, DYNTYPE_STRING);

    if (ret == WS_SUCCESS) {
        /* increase amount to be sent only if BundlePacket will be called */
        ssh->outputBuffer.length = idx;
        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    if (ret != WS_WANT_WRITE && ret != WS_SUCCESS)
        PurgePacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendKexInit(), ret = %d", ret);
    return ret;
}


struct wolfSSH_sigKeyBlockFull {
        byte pubKeyId;
        byte sigId;
        word32 sz;
        const char *name;
        word32 nameSz;
        union {
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
            struct {
                RsaKey key;
                byte e[257];
                word32 eSz;
                byte ePad;
                byte n[257];
                word32 nSz;
                byte nPad;
            } rsa;
#endif
#ifndef WOLFSSH_NO_ECDSA
            struct {
                ecc_key key;
                word32 keyBlobSz;
                const char *keyBlobName;
                word32 keyBlobNameSz;
                byte q[257];
                word32 qSz;
                byte qPad;
                const char *primeName;
                word32 primeNameSz;
            } ecc;
#endif
        } sk;
};

#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
    /* Size of Kyber public key (bigger than ciphertext) and some extra for the
     * ECC hybrid component. */
    #define KEX_F_SIZE 1024
#else
    #define KEX_F_SIZE (256 + 1)
#endif

#define KEX_SIG_SIZE (512)

#ifdef WOLFSSH_CERTS
/* places RFC6187 style cert + ocsp into output buffer and advances idx
 * [size of stiring] [string] [cert count] [cert size] [cert] [...]
 *                            [ocsp count] [ocsp size] [ocsp] [...]
 * returns WS_SUCCESS on success
 * returns LENGTH_ONLY_E if output is null, and updates outputSz with required
 *      output buffer size
 */
static int BuildRFC6187Info(WOLFSSH* ssh, int pubKeyID,
            const byte* cert, word32 certSz,
            const byte* ocsp, word32 ocspSz,
            byte* output, word32* outputSz, word32* idx)
{
    int ret = WS_SUCCESS;
    word32 localIdx;
    const byte* publicKeyType;
    word32 publicKeyTypeSz;

    localIdx = *idx;
    switch (pubKeyID) {
        #ifndef WOLFSSH_NO_SSH_RSA_SHA1
        case ID_X509V3_SSH_RSA:
            publicKeyType = (const byte*)cannedKeyAlgoX509RsaNames;
            break;
        #endif

        #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
        case ID_X509V3_ECDSA_SHA2_NISTP256:
            publicKeyType = (const byte*)cannedKeyAlgoX509Ecc256Names;
            break;
        #endif

        #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
        case ID_X509V3_ECDSA_SHA2_NISTP384:
            publicKeyType = (const byte*)cannedKeyAlgoX509Ecc384Names;
            break;
        #endif

        #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
        case ID_X509V3_ECDSA_SHA2_NISTP521:
            publicKeyType = (const byte*)cannedKeyAlgoX509Ecc521Names;
            break;
        #endif

        default:
            return WS_BAD_ARGUMENT;
    }
    publicKeyTypeSz = (word32)WSTRLEN((const char*)publicKeyType);

    /* length of entire bundle of info */
    if (output) {
        c32toa((LENGTH_SZ * 2) + (UINT32_SZ * 2) +
            publicKeyTypeSz + certSz, output + localIdx);
    }
    localIdx += LENGTH_SZ;

    /* add public key type */
    if (output)
        c32toa(publicKeyTypeSz, output + localIdx);
    localIdx += LENGTH_SZ;
    if (output)
        WMEMCPY(output + localIdx, publicKeyType, publicKeyTypeSz);
    localIdx += publicKeyTypeSz;

    /* add cert count (hard set to 1 cert for now @TODO) */
    if (output)
        c32toa(1, output + localIdx);
    localIdx += UINT32_SZ;

    /* add in certificates, note this could later be multiple [certsz][cert] */
    if (output)
        c32toa(certSz, output + localIdx);
    localIdx += LENGTH_SZ;
    if (output)
        WMEMCPY(output + localIdx, cert, certSz);
    localIdx += certSz;

    /* add in ocsp count hard set to 0 */
    if (output)
        c32toa(0, output + localIdx); /* ocsp count */
    localIdx += UINT32_SZ;

    /* here is where OCSP's would be appended [ocsp size][ocsp] */
    WOLFSSH_UNUSED(ocsp);
    WOLFSSH_UNUSED(ocspSz);

    /* update idx on success */
    if (output) {
        *idx = localIdx;
    }
    else {
        *outputSz = localIdx - *idx;
        ret = LENGTH_ONLY_E;
    }

    WOLFSSH_UNUSED(ssh);
    WOLFSSH_UNUSED(outputSz);
    return ret;
}
#endif /* WOLFSSH_CERTS */


/* Sets the signing key and hashes in the public key
 * returns WS_SUCCESS on success */
static int SendKexGetSigningKey(WOLFSSH* ssh,
        struct wolfSSH_sigKeyBlockFull *sigKeyBlock_ptr,
        enum wc_HashType enmhashId, word32 keyIdx)
{
    int ret;
    byte isCert = 0;
    void* heap;
    byte scratchLen[LENGTH_SZ];
    word32 scratch = 0;
#ifndef WOLFSSH_NO_DH
    const byte* primeGroup = NULL;
    word32 primeGroupSz = 0;
    const byte* generator = NULL;
    word32 generatorSz = 0;
#endif


    heap = ssh->ctx->heap;

    #ifdef WOLFSSH_CERTS
    switch (sigKeyBlock_ptr->pubKeyId) {
        case ID_X509V3_SSH_RSA:
        case ID_X509V3_ECDSA_SHA2_NISTP256:
        case ID_X509V3_ECDSA_SHA2_NISTP384:
        case ID_X509V3_ECDSA_SHA2_NISTP521:
        isCert = 1;
    }
    #endif

    switch (sigKeyBlock_ptr->sigId) {
        #ifndef WOLFSSH_NO_SSH_RSA_SHA1
        case ID_SSH_RSA:
            /* Decode the user-configured RSA private key. */
            sigKeyBlock_ptr->sk.rsa.eSz = sizeof(sigKeyBlock_ptr->sk.rsa.e);
            sigKeyBlock_ptr->sk.rsa.nSz = sizeof(sigKeyBlock_ptr->sk.rsa.n);
            ret = wc_InitRsaKey(&sigKeyBlock_ptr->sk.rsa.key, heap);
            if (ret == 0)
                ret = wc_RsaPrivateKeyDecode(ssh->ctx->privateKey[keyIdx],
                        &scratch, &sigKeyBlock_ptr->sk.rsa.key,
                        (int)ssh->ctx->privateKeySz[keyIdx]);

            /* hash in usual public key if not RFC6187 style cert use */
            if (!isCert) {
                /* Flatten the public key into mpint values for the hash. */
                if (ret == 0)
                    ret = wc_RsaFlattenPublicKey(&sigKeyBlock_ptr->sk.rsa.key,
                                                 sigKeyBlock_ptr->sk.rsa.e,
                                                 &sigKeyBlock_ptr->sk.rsa.eSz,
                                                 sigKeyBlock_ptr->sk.rsa.n,
                                                 &sigKeyBlock_ptr->sk.rsa.nSz);
                if (ret == 0) {
                    /* Add a pad byte if the mpint has the MSB set. */
                    ret = CreateMpint(sigKeyBlock_ptr->sk.rsa.e,
                            &sigKeyBlock_ptr->sk.rsa.eSz,
                            &sigKeyBlock_ptr->sk.rsa.ePad);
                }
                if (ret == 0) {
                    /* Add a pad byte if the mpint has the MSB set. */
                    ret = CreateMpint(sigKeyBlock_ptr->sk.rsa.n,
                            &sigKeyBlock_ptr->sk.rsa.nSz,
                            &sigKeyBlock_ptr->sk.rsa.nPad);
                }
                if (ret == 0) {
                    sigKeyBlock_ptr->sz = (LENGTH_SZ * 3) +
                                      sigKeyBlock_ptr->nameSz +
                                      sigKeyBlock_ptr->sk.rsa.eSz +
                                      sigKeyBlock_ptr->sk.rsa.ePad +
                                      sigKeyBlock_ptr->sk.rsa.nSz +
                                      sigKeyBlock_ptr->sk.rsa.nPad;
                    c32toa(sigKeyBlock_ptr->sz, scratchLen);
                    /* Hash in the length of the public key block. */
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the length of the key type string. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->nameSz, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the key type string. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        (byte*)sigKeyBlock_ptr->name,
                                        sigKeyBlock_ptr->nameSz);
                /* Hash in the length of the RSA public key E value. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->sk.rsa.eSz +
                        sigKeyBlock_ptr->sk.rsa.ePad, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the pad byte for the RSA public key E value. */
                if (ret == 0) {
                    if (sigKeyBlock_ptr->sk.rsa.ePad) {
                        scratchLen[0] = 0;
                        ret = HashUpdate(&ssh->handshake->hash,
                                            enmhashId, scratchLen, 1);
                    }
                }
                /* Hash in the RSA public key E value. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        sigKeyBlock_ptr->sk.rsa.e,
                                        sigKeyBlock_ptr->sk.rsa.eSz);
                /* Hash in the length of the RSA public key N value. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->sk.rsa.nSz +
                            sigKeyBlock_ptr->sk.rsa.nPad, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the pad byte for the RSA public key N value. */
                if (ret == 0) {
                    if (sigKeyBlock_ptr->sk.rsa.nPad) {
                        scratchLen[0] = 0;
                        ret = HashUpdate(&ssh->handshake->hash,
                                            enmhashId, scratchLen, 1);
                    }
                }
                /* Hash in the RSA public key N value. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        sigKeyBlock_ptr->sk.rsa.n,
                                        sigKeyBlock_ptr->sk.rsa.nSz);
            }
            break;
        #endif /* WOLFSSH_NO_SSH_RSA_SHA1 */

        #ifndef WOLFSSH_NO_ECDSA
        case ID_ECDSA_SHA2_NISTP256:
        case ID_ECDSA_SHA2_NISTP384:
        case ID_ECDSA_SHA2_NISTP521:
            sigKeyBlock_ptr->sk.ecc.primeName =
                    PrimeNameForId(ssh->handshake->sigId);
            sigKeyBlock_ptr->sk.ecc.primeNameSz =
                    (word32)strlen(sigKeyBlock_ptr->sk.ecc.primeName);

            /* Decode the user-configured ECDSA private key. */
            sigKeyBlock_ptr->sk.ecc.qSz = sizeof(sigKeyBlock_ptr->sk.ecc.q);
            ret = wc_ecc_init_ex(&sigKeyBlock_ptr->sk.ecc.key, heap,
                    INVALID_DEVID);
            scratch = 0;
            if (ret == 0)
                ret = wc_EccPrivateKeyDecode(ssh->ctx->privateKey[keyIdx],
                        &scratch, &sigKeyBlock_ptr->sk.ecc.key,
                        ssh->ctx->privateKeySz[keyIdx]);

            /* hash in usual public key if not RFC6187 style cert use */
            if (!isCert) {
                /* Flatten the public key into x963 value for hash. */
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_export_x963(&sigKeyBlock_ptr->sk.ecc.key,
                                             sigKeyBlock_ptr->sk.ecc.q,
                                             &sigKeyBlock_ptr->sk.ecc.qSz);
                    PRIVATE_KEY_LOCK();
                }
                /* Hash in the length of the public key block. */
                if (ret == 0) {
                    sigKeyBlock_ptr->sz = (LENGTH_SZ * 3) +
                                     sigKeyBlock_ptr->nameSz +
                                     sigKeyBlock_ptr->sk.ecc.primeNameSz +
                                     sigKeyBlock_ptr->sk.ecc.qSz;
                    c32toa(sigKeyBlock_ptr->sz, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the length of the key type string. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->nameSz, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the key type string. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        (byte*)sigKeyBlock_ptr->name,
                                        sigKeyBlock_ptr->nameSz);
                /* Hash in the length of the name of the prime. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->sk.ecc.primeNameSz, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the name of the prime. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                 (const byte*)sigKeyBlock_ptr->sk.ecc.primeName,
                                 sigKeyBlock_ptr->sk.ecc.primeNameSz);
                /* Hash in the length of the public key. */
                if (ret == 0) {
                    c32toa(sigKeyBlock_ptr->sk.ecc.qSz, scratchLen);
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        scratchLen, LENGTH_SZ);
                }
                /* Hash in the public key. */
                if (ret == 0)
                    ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                        sigKeyBlock_ptr->sk.ecc.q,
                                        sigKeyBlock_ptr->sk.ecc.qSz);
            }
            break;
        #endif

            default:
                ret = WS_INVALID_ALGO_ID;
        }


        /* if is RFC6187 then the hash of the public key is changed */
        if (ret == 0 && isCert) {
        #ifdef WOLFSSH_CERTS
            byte* tmp;
            word32 idx = 0;

            BuildRFC6187Info(ssh, sigKeyBlock_ptr->pubKeyId,
                ssh->ctx->cert[keyIdx], ssh->ctx->certSz[keyIdx], NULL, 0,
                NULL, &sigKeyBlock_ptr->sz, &idx);
            tmp = (byte*)WMALLOC(sigKeyBlock_ptr->sz, heap, DYNTYPE_TEMP);
            if (tmp == NULL) {
                ret = WS_MEMORY_E;
            }
            else {
                idx = 0;
                BuildRFC6187Info(ssh, sigKeyBlock_ptr->pubKeyId,
                    ssh->ctx->cert[keyIdx], ssh->ctx->certSz[keyIdx], NULL, 0,
                    tmp, &sigKeyBlock_ptr->sz, &idx);
                ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                    tmp, sigKeyBlock_ptr->sz);
                WFREE(tmp, heap, DYNTYPE_TEMP);
            }
        #else
            ret = WS_NOT_COMPILED;
        #endif
        }


#ifndef WOLFSSH_NO_DH_GEX_SHA256
        /* If using DH-GEX include the GEX specific values. */
        if (ssh->handshake->kexId == ID_DH_GEX_SHA256) {
            byte primeGroupPad = 0, generatorPad = 0;

            /* Hash in the client's requested minimum key size. */
            if (ret == 0) {
                c32toa(ssh->handshake->dhGexMinSz, scratchLen);
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, LENGTH_SZ);
            }
            /* Hash in the client's requested preferred key size. */
            if (ret == 0) {
                c32toa(ssh->handshake->dhGexPreferredSz, scratchLen);
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, LENGTH_SZ);
            }
            /* Hash in the client's requested maximum key size. */
            if (ret == 0) {
                c32toa(ssh->handshake->dhGexMaxSz, scratchLen);
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, LENGTH_SZ);
            }
            /* Add a pad byte if the mpint has the MSB set. */
            if (ret == 0) {
                ret = CreateMpint((byte*)primeGroup,
                        &primeGroupSz, &primeGroupPad);
            }
            if (ret == 0) {
                /* Hash in the length of the GEX prime group. */
                c32toa(primeGroupSz + primeGroupPad, scratchLen);
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, LENGTH_SZ);
            }
            /* Hash in the pad byte for the GEX prime group. */
            if (ret == 0) {
                if (primeGroupPad) {
                    scratchLen[0] = 0;
                    ret = HashUpdate(&ssh->handshake->hash,
                                        enmhashId,
                                        scratchLen, 1);
                }
            }
            /* Hash in the GEX prime group. */
            if (ret == 0)
                ret  = HashUpdate(&ssh->handshake->hash,
                                     enmhashId,
                                     primeGroup, primeGroupSz);
            /* Add a pad byte if the mpint has the MSB set. */
            if (ret == 0) {
                ret = CreateMpint((byte*)generator,
                        &generatorSz, &generatorPad);
            }
            if (ret == 0) {
                /* Hash in the length of the GEX generator. */
                c32toa(generatorSz + generatorPad, scratchLen);
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    scratchLen, LENGTH_SZ);
            }
            /* Hash in the pad byte for the GEX generator. */
            if (ret == 0) {
                if (generatorPad) {
                    scratchLen[0] = 0;
                    ret = HashUpdate(&ssh->handshake->hash,
                                        enmhashId,
                                        scratchLen, 1);
                }
            }
            /* Hash in the GEX generator. */
            if (ret == 0)
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId,
                                    generator, generatorSz);
        }
#endif

        /* Hash in the size of the client's DH e-value (ECDH Q-value). */
        if (ret == 0) {
            c32toa(ssh->handshake->eSz, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        /* Hash in the client's DH e-value (ECDH Q-value). */
        if (ret == 0)
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                ssh->handshake->e, ssh->handshake->eSz);

    return (ret == 0)? WS_SUCCESS : ret;
}


/* SendKexDhReply()
 * It is also the funciton used for MSGID_KEXECDH_REPLY. The parameters
 * are analogous between the two messages. Where MSGID_KEXDH_REPLY has
 * server's public host key (K_S), f, and the signature of H;
 * MSGID_KEXECDH_REPLY has K_S, the server'e ephemeral public key (Q_S),
 * and the signature of H. This also applies to the GEX version of this.
 * H is calculated the same for KEXDH and KEXECDH, and has some exceptions
 * for GEXDH. */
int SendKexDhReply(WOLFSSH* ssh)
{
    int ret = WS_SUCCESS;
    void *heap  = NULL;
    byte *f_ptr = NULL, *sig_ptr = NULL;
#ifndef WOLFSSH_NO_ECDH
    byte *r_ptr = NULL, *s_ptr = NULL;
#endif
    byte scratchLen[LENGTH_SZ];
    word32 fSz = KEX_F_SIZE;
    word32 sigSz = KEX_SIG_SIZE;
    byte useEcc = 0;
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
    byte useEccKyber = 0;
#endif
    byte fPad = 0;
    byte kPad = 0;
    word32 sigBlockSz = 0;
    word32 payloadSz = 0;
    byte* output;
    word32 idx;
    word32 keyIdx = 0;
    byte msgId = MSGID_KEXDH_REPLY;
    enum wc_HashType enmhashId = WC_HASH_TYPE_NONE;
#ifndef WOLFSSH_NO_DH
    byte *y_ptr = NULL;
    const byte* primeGroup = NULL;
    word32 primeGroupSz = 0;
    const byte* generator = NULL;
    word32 generatorSz = 0;
#endif
    struct wolfSSH_sigKeyBlockFull *sigKeyBlock_ptr = NULL;
#ifndef WOLFSSH_SMALL_STACK
    byte f_s[KEX_F_SIZE];
    byte sig_s[KEX_SIG_SIZE];

    f_ptr = f_s;
    sig_ptr = sig_s;
#endif
    WLOG(WS_LOG_DEBUG, "Entering SendKexDhReply()");

    if (ret == WS_SUCCESS) {
        if (ssh == NULL || ssh->ctx == NULL || ssh->handshake == NULL) {
            ret = WS_BAD_ARGUMENT;
        }
    }

    if (ret == WS_SUCCESS) {
        heap = ssh->ctx->heap;
    }

#ifdef WOLFSSH_SMALL_STACK
    f_ptr = (byte*)WMALLOC(KEX_F_SIZE, heap, DYNTYPE_BUFFER);
    sig_ptr = (byte*)WMALLOC(KEX_SIG_SIZE, heap, DYNTYPE_BUFFER);
    if (f_ptr == NULL || sig_ptr == NULL)
        ret = WS_MEMORY_E;
#endif

    sigKeyBlock_ptr = (struct wolfSSH_sigKeyBlockFull*)WMALLOC(
            sizeof(struct wolfSSH_sigKeyBlockFull), heap, DYNTYPE_PRIVKEY);
    if (sigKeyBlock_ptr == NULL)
        ret = WS_MEMORY_E;

    if (ret == WS_SUCCESS) {
        WMEMSET(sigKeyBlock_ptr, 0, sizeof(struct wolfSSH_sigKeyBlockFull));
        sigKeyBlock_ptr->pubKeyId = ID_NONE;
    }

    if (ret == WS_SUCCESS) {
        sigKeyBlock_ptr->pubKeyId = ssh->handshake->pubKeyId;
        sigKeyBlock_ptr->sigId = ssh->handshake->sigId;
        sigKeyBlock_ptr->name = IdToName(ssh->handshake->sigId);
        sigKeyBlock_ptr->nameSz = (word32)strlen(sigKeyBlock_ptr->name);

        switch (ssh->handshake->kexId) {
#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
            case ID_DH_GROUP1_SHA1:
                primeGroup = dhPrimeGroup1;
                primeGroupSz = dhPrimeGroup1Sz;
                generator = dhGenerator;
                generatorSz = dhGeneratorSz;
                break;
#endif
#ifndef WOLFSSH_NO_DH_GROUP14_SHA1
            case ID_DH_GROUP14_SHA1:
                primeGroup = dhPrimeGroup14;
                primeGroupSz = dhPrimeGroup14Sz;
                generator = dhGenerator;
                generatorSz = dhGeneratorSz;
                break;
#endif
#ifndef WOLFSSH_NO_DH_GEX_SHA256
            case ID_DH_GEX_SHA256:
                primeGroup = dhPrimeGroup14;
                primeGroupSz = dhPrimeGroup14Sz;
                generator = dhGenerator;
                generatorSz = dhGeneratorSz;
                msgId = MSGID_KEXDH_GEX_REPLY;
                break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
            case ID_ECDH_SHA2_NISTP256:
                useEcc = 1;
                msgId = MSGID_KEXDH_REPLY;
                break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
            case ID_ECDH_SHA2_NISTP384:
                useEcc = 1;
                msgId = MSGID_KEXDH_REPLY;
                break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
            case ID_ECDH_SHA2_NISTP521:
                useEcc = 1;
                msgId = MSGID_KEXDH_REPLY;
                break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
            case ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256:
                useEccKyber = 1; /* Only support level 1 for now. */
                msgId = MSGID_KEXKEM_REPLY;
                break;
#endif
            default:
                ret = WS_INVALID_ALGO_ID;
        }

        enmhashId = (enum wc_HashType)ssh->handshake->hashId;
    }

    if (ret == WS_SUCCESS) {
        for (keyIdx = 0; keyIdx < ssh->ctx->privateKeyCount; keyIdx++) {
            if (ssh->ctx->privateKeyId[keyIdx] == ssh->handshake->pubKeyId) {
                break;
            }
        }
        if (keyIdx == ssh->ctx->privateKeyCount) {
            ret = WS_INVALID_ALGO_ID;
        }
    }

    /* At this point, the exchange hash, H, includes items V_C, V_S, I_C,
     * and I_S. Next add K_S, the server's public host key. K_S will
     * either be RSA or ECDSA public key blob. */
    if (ret == WS_SUCCESS) {
        ret = SendKexGetSigningKey(ssh, sigKeyBlock_ptr, enmhashId, keyIdx);
    }

    if (ret == WS_SUCCESS) {
        /* reset size here because a previous shared secret could potentially be
         * smaller by a byte than usual and cause buffer issues with re-key */
        if (ret == 0)
            ssh->kSz = MAX_KEX_KEY_SZ;

        /* Make the server's DH f-value and the shared secret K. */
        /* Or make the server's ECDH private value, and the shared secret K. */
        if (ret == 0) {
            if (!useEcc
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
                && !useEccKyber
#endif
               ) {
#ifndef WOLFSSH_NO_DH
                word32 ySz = MAX_KEX_KEY_SZ;
            #ifdef WOLFSSH_SMALL_STACK
                DhKey *privKey = (DhKey*)WMALLOC(sizeof(DhKey), heap,
                        DYNTYPE_PRIVKEY);
                y_ptr = (byte*)WMALLOC(ySz, heap, DYNTYPE_PRIVKEY);
                if (privKey == NULL || y_ptr == NULL)
                    ret = WS_MEMORY_E;
            #else
                DhKey privKey[1];
                byte y_s[MAX_KEX_KEY_SZ];
                y_ptr = y_s;
            #endif
                if (ret == WS_SUCCESS) {
                    ret = wc_InitDhKey(privKey);
                    if (ret == 0)
                        ret = wc_DhSetKey(privKey, primeGroup, primeGroupSz,
                                generator, generatorSz);
                    if (ret == 0)
                        ret = wc_DhGenerateKeyPair(privKey, ssh->rng,
                                y_ptr, &ySz, f_ptr, &fSz);
                    if (ret == 0) {
                        PRIVATE_KEY_UNLOCK();
                        ret = wc_DhAgree(privKey, ssh->k, &ssh->kSz, y_ptr, ySz,
                                ssh->handshake->e, ssh->handshake->eSz);
                        PRIVATE_KEY_LOCK();
                    }
                    ForceZero(y_ptr, ySz);
                    wc_FreeDhKey(privKey);
                }
            #ifdef WOLFSSH_SMALL_STACK
                if (privKey) {
                    WFREE(privKey, heap, DYNTYPE_PRIVKEY);
                }
            #endif
#endif /* ! WOLFSSH_NO_DH */
            }
            else if (useEcc) {
#if !defined(WOLFSSH_NO_ECDH)
            #ifdef WOLFSSH_SMALL_STACK
                ecc_key *pubKey = NULL, *privKey = NULL;
                pubKey = (ecc_key*)WMALLOC(sizeof(ecc_key), heap,
                        DYNTYPE_PUBKEY);
                privKey = (ecc_key*)WMALLOC(sizeof(ecc_key), heap,
                        DYNTYPE_PRIVKEY);
                if (pubKey == NULL || privKey == NULL) {
                    ret = WS_MEMORY_E;
                }
            #else
                ecc_key pubKey[1];
                ecc_key privKey[1];
            #endif
                int primeId;

                primeId  = wcPrimeForId(ssh->handshake->kexId);
                if (primeId == ECC_CURVE_INVALID)
                    ret = WS_INVALID_PRIME_CURVE;

                if (ret == 0)
                    ret = wc_ecc_init_ex(pubKey, heap, INVALID_DEVID);
                if (ret == 0)
                    ret = wc_ecc_init_ex(privKey, heap, INVALID_DEVID);
#ifdef HAVE_WC_ECC_SET_RNG
                if (ret == 0)
                    ret = wc_ecc_set_rng(privKey, ssh->rng);
#endif

                if (ret == 0)
                    ret = wc_ecc_import_x963_ex(ssh->handshake->e,
                                                ssh->handshake->eSz,
                                                pubKey, primeId);

                if (ret == 0)
                    ret = wc_ecc_make_key_ex(ssh->rng,
                                         wc_ecc_get_curve_size_from_id(primeId),
                                         privKey, primeId);
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_export_x963(privKey, f_ptr, &fSz);
                    PRIVATE_KEY_LOCK();
                }
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_shared_secret(privKey, pubKey,
                                               ssh->k, &ssh->kSz);
                    PRIVATE_KEY_LOCK();
                }
                wc_ecc_free(privKey);
                wc_ecc_free(pubKey);
            #ifdef WOLFSSH_SMALL_STACK
                WFREE(pubKey, heap, DYNTYPE_PUBKEY);
                WFREE(privKey, heap, DYNTYPE_PRIVKEY);
                pubKey  = NULL;
                privKey = NULL;
            #endif
#endif /* !defined(WOLFSSH_NO_ECDH) */
            }
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
            else if (useEccKyber) {
                /* This is a hybrid KEM. In this case, I need to generate my ECC
                 * keypair, send the public one, use the private one to generate
                 * the shared secret, use the post-quantum public key to
                 * generate and encapsulate the shared secret and send the
                 * ciphertext. */
                OQS_KEM* kem = NULL;
                int primeId;
                ret = 0;
            #ifdef WOLFSSH_SMALL_STACK
                ecc_key *pubKey = NULL, *privKey = NULL;
                pubKey = (ecc_key*)WMALLOC(sizeof(ecc_key), heap,
                        DYNTYPE_PUBKEY);
                privKey = (ecc_key*)WMALLOC(sizeof(ecc_key), heap,
                        DYNTYPE_PRIVKEY);
                if (pubKey == NULL || privKey == NULL) {
                    ret = WS_MEMORY_E;
                }
            #else
                ecc_key pubKey[1];
                ecc_key privKey[1];
            #endif

                XMEMSET(pubKey, 0, sizeof(*pubKey));
                XMEMSET(privKey, 0, sizeof(*privKey));

                if (ret == 0) {
                    kem = OQS_KEM_new(OQS_KEM_alg_kyber_512);
                    if (kem == NULL) {
                        ret = WS_INVALID_ALGO_ID;
                    }
                }

                if (ssh->handshake->eSz <= (word32)kem->length_public_key) {
                    ret = WS_BUFFER_E;
                }

                if (ret == 0) {
                    primeId = wcPrimeForId(ssh->handshake->kexId);
                    if (primeId == ECC_CURVE_INVALID)
                        ret = WS_INVALID_PRIME_CURVE;
                }

                if (ret == 0)
                    ret = wc_ecc_init_ex(pubKey, heap, INVALID_DEVID);
                if (ret == 0)
                    ret = wc_ecc_init_ex(privKey, heap, INVALID_DEVID);
#ifdef HAVE_WC_ECC_SET_RNG
                if (ret == 0)
                    ret = wc_ecc_set_rng(privKey, ssh->rng);
#endif

                if (ret == 0)
                    ret = wc_ecc_import_x963_ex(ssh->handshake->e,
                              ssh->handshake->eSz -
                              (word32)kem->length_public_key,
                              pubKey, primeId);

                if (ret == 0)
                    ret = wc_ecc_make_key_ex(ssh->rng,
                              wc_ecc_get_curve_size_from_id(primeId),
                              privKey, primeId);
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_export_x963(privKey, f_ptr, &fSz);
                    PRIVATE_KEY_LOCK();
                }
                if (ret == 0) {
                    PRIVATE_KEY_UNLOCK();
                    ret = wc_ecc_shared_secret(privKey, pubKey,
                                               ssh->k, &ssh->kSz);
                    PRIVATE_KEY_LOCK();
                }
                wc_ecc_free(privKey);
                wc_ecc_free(pubKey);
            #ifdef WOLFSSH_SMALL_STACK
                WFREE(pubKey, heap, DYNTYPE_PUBKEY);
                WFREE(privKey, heap, DYNTYPE_PRIVKEY);
                pubKey  = NULL;
                privKey = NULL;
            #endif

                if (ret == 0) {
                    if (OQS_KEM_encaps(kem, f_ptr + fSz, ssh->k + ssh->kSz,
                        ssh->handshake->e + ssh->handshake->eSz
                        - kem->length_public_key)
                        != OQS_SUCCESS) {
                        ret = WS_PUBKEY_REJECTED_E;
                    }
                }

                if (ret == 0) {
                    fSz += kem->length_ciphertext;
                    ssh->kSz += kem->length_shared_secret;
                } else {
                    fSz = 0;
                    ssh->kSz = 0;
                    WLOG(WS_LOG_ERROR,
                         "Generate ECC-kyber (encap) shared secret failed, %d",
                         ret);
                }

                if (kem != NULL) {
                    OQS_KEM_free(kem);
                }
            }
#endif
            else {
                /* This should never happen */
                ret = WS_ERROR;
            }
        }

        /* Hash in the server's DH f-value. */
        if (ret == 0) {
            ret = CreateMpint(f_ptr, &fSz, &fPad);
        }
        if (ret == 0) {
            c32toa(fSz + fPad, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                               scratchLen, LENGTH_SZ);
        }
        if (ret == 0) {
            if (fPad) {
                scratchLen[0] = 0;
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId, scratchLen, 1);
            }
        }

        if (ret == 0) {
            ret = HashUpdate(&ssh->handshake->hash,
                                enmhashId, f_ptr, fSz);
        }

        /* Hash in the shared secret K. */
        if (ret == 0) {
            ret = CreateMpint(ssh->k, &ssh->kSz, &kPad);
        }
        if (ret == 0) {
            c32toa(ssh->kSz + kPad, scratchLen);
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                scratchLen, LENGTH_SZ);
        }
        if (ret == 0) {
            if (kPad) {
                scratchLen[0] = 0;
                ret = HashUpdate(&ssh->handshake->hash,
                                    enmhashId, scratchLen, 1);
            }
        }

        if (ret == 0) {
            ret = HashUpdate(&ssh->handshake->hash, enmhashId,
                                ssh->k, ssh->kSz);
        }

        /* Save the exchange hash value H, and session ID. */
        if (ret == 0) {
            ret = wc_HashFinal(&ssh->handshake->hash,
                               enmhashId, ssh->h);
            wc_HashFree(&ssh->handshake->hash, enmhashId);
            ssh->handshake->hashId = WC_HASH_TYPE_NONE;
        }

        if (ret == 0) {
            ssh->hSz = wc_HashGetDigestSize(enmhashId);
            if (ssh->sessionIdSz == 0) {
                WMEMCPY(ssh->sessionId, ssh->h, ssh->hSz);
                ssh->sessionIdSz = ssh->hSz;
            }
        }

        if (ret != WS_SUCCESS)
            ret = WS_CRYPTO_FAILED;
    }

    /* Sign h with the server's private key. */
    if (ret == WS_SUCCESS) {
        wc_HashAlg digestHash;
        byte digest[WC_MAX_DIGEST_SIZE];
        enum wc_HashType sigHashId;

        sigHashId = HashForId(ssh->handshake->pubKeyId);

        ret = wc_HashInit(&digestHash, sigHashId);
        if (ret == 0)
            ret = HashUpdate(&digestHash, sigHashId, ssh->h, ssh->hSz);
        if (ret == 0)
            ret = wc_HashFinal(&digestHash, sigHashId, digest);
        if (ret != 0)
            ret = WS_CRYPTO_FAILED;
        wc_HashFree(&digestHash, sigHashId);

        if (ret == WS_SUCCESS) {
            if (sigKeyBlock_ptr->pubKeyId == ID_SSH_RSA
        #ifdef WOLFSSH_CERTS
             || sigKeyBlock_ptr->pubKeyId == ID_X509V3_SSH_RSA
        #endif
            ) {
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
                word32 encSigSz;
            #ifdef WOLFSSH_SMALL_STACK
                byte *encSig = (byte*)WMALLOC(MAX_ENCODED_SIG_SZ, heap,
                        DYNTYPE_TEMP);
                if (encSig == NULL) {
                    ret = WS_MEMORY_E;
                }

                if (ret == WS_SUCCESS)
            #else
                byte encSig[MAX_ENCODED_SIG_SZ];
            #endif
                {
                    encSigSz = wc_EncodeSignature(encSig, digest,
                                              wc_HashGetDigestSize(sigHashId),
                                              wc_HashGetOID(sigHashId));
                    if (encSigSz <= 0) {
                        WLOG(WS_LOG_DEBUG, "SendKexDhReply: Bad Encode Sig");
                        ret = WS_CRYPTO_FAILED;
                    }
                    else {
                        WLOG(WS_LOG_INFO, "Signing hash with %s.",
                            IdToName(ssh->handshake->pubKeyId));
                        sigSz = wc_RsaSSL_Sign(encSig, encSigSz, sig_ptr,
                                KEX_SIG_SIZE, &sigKeyBlock_ptr->sk.rsa.key,
                                ssh->rng);
                        if (sigSz <= 0) {
                            WLOG(WS_LOG_DEBUG, "SendKexDhReply: Bad RSA Sign");
                            ret = WS_RSA_E;
                        }
                    }
                #ifdef WOLFSSH_SMALL_STACK
                    WFREE(encSig, heap, DYNTYPE_TEMP);
                #endif
                }
#endif
            }
            else if (sigKeyBlock_ptr->pubKeyId == ID_ECDSA_SHA2_NISTP256 ||
                    sigKeyBlock_ptr->pubKeyId == ID_ECDSA_SHA2_NISTP384 ||
                    sigKeyBlock_ptr->pubKeyId == ID_ECDSA_SHA2_NISTP521
#ifdef WOLFSSH_CERTS
                 || sigKeyBlock_ptr->pubKeyId == ID_X509V3_ECDSA_SHA2_NISTP256 ||
                    sigKeyBlock_ptr->pubKeyId == ID_X509V3_ECDSA_SHA2_NISTP384 ||
                    sigKeyBlock_ptr->pubKeyId == ID_X509V3_ECDSA_SHA2_NISTP521
#endif
            ) {
#ifndef WOLFSSH_NO_ECDSA
                WLOG(WS_LOG_INFO, "Signing hash with %s.",
                        IdToName(ssh->handshake->pubKeyId));
                sigSz = KEX_SIG_SIZE;
                ret = wc_ecc_sign_hash(digest, wc_HashGetDigestSize(sigHashId),
                                       sig_ptr, &sigSz,
                                       ssh->rng, &sigKeyBlock_ptr->sk.ecc.key);
                if (ret != MP_OKAY) {
                    WLOG(WS_LOG_DEBUG, "SendKexDhReply: Bad ECDSA Sign");
                    ret = WS_ECC_E;
                }
                else {
                    word32 rSz = MAX_ECC_BYTES + ECC_MAX_PAD_SZ;
                    word32 sSz = MAX_ECC_BYTES + ECC_MAX_PAD_SZ;
                    byte rPad;
                    byte sPad;
#ifdef WOLFSSH_SMALL_STACK
                    r_ptr = (byte*)WMALLOC(rSz, heap, DYNTYPE_BUFFER);
                    s_ptr = (byte*)WMALLOC(sSz, heap, DYNTYPE_BUFFER);
                    if (r_ptr == NULL || s_ptr == NULL)
                        ret = WS_MEMORY_E;
#else
                    byte r_s[MAX_ECC_BYTES + ECC_MAX_PAD_SZ];
                    byte s_s[MAX_ECC_BYTES + ECC_MAX_PAD_SZ];
                    r_ptr = r_s;
                    s_ptr = s_s;
#endif
                    if (ret == WS_SUCCESS)
                        ret = wc_ecc_sig_to_rs(sig_ptr, sigSz, r_ptr, &rSz, s_ptr, &sSz);
                    if (ret == 0) {
                        idx = 0;
                        rPad = (r_ptr[0] & 0x80) ? 1 : 0;
                        sPad = (s_ptr[0] & 0x80) ? 1 : 0;
                        sigSz = (LENGTH_SZ * 2) + rSz + rPad + sSz + sPad;

                        c32toa(rSz + rPad, sig_ptr + idx);
                        idx += LENGTH_SZ;
                        if (rPad)
                            sig_ptr[idx++] = 0;
                        WMEMCPY(sig_ptr + idx, r_ptr, rSz);
                        idx += rSz;
                        c32toa(sSz + sPad, sig_ptr + idx);
                        idx += LENGTH_SZ;
                        if (sPad)
                            sig_ptr[idx++] = 0;
                        WMEMCPY(sig_ptr + idx, s_ptr, sSz);
                    }
                }
#endif
            }
        }
    }

    if (sigKeyBlock_ptr != NULL) {
        if (sigKeyBlock_ptr->sigId == ID_SSH_RSA) {
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
            wc_FreeRsaKey(&sigKeyBlock_ptr->sk.rsa.key);
#endif
        }
        else if (sigKeyBlock_ptr->sigId == ID_ECDSA_SHA2_NISTP256 ||
                sigKeyBlock_ptr->sigId == ID_ECDSA_SHA2_NISTP384 ||
                sigKeyBlock_ptr->sigId == ID_ECDSA_SHA2_NISTP521) {
#ifndef WOLFSSH_NO_ECDSA
            wc_ecc_free(&sigKeyBlock_ptr->sk.ecc.key);
#endif
        }
    }

    if (ret == WS_SUCCESS)
        ret = GenerateKeys(ssh, enmhashId);

    /* Get the buffer, copy the packet data, once f is laid into the buffer,
     * add it to the hash and then add K. */
    if (ret == WS_SUCCESS) {
        sigBlockSz = (LENGTH_SZ * 2) + sigKeyBlock_ptr->nameSz + sigSz;
        payloadSz = MSG_ID_SZ + (LENGTH_SZ * 3) +
                    sigKeyBlock_ptr->sz + fSz + fPad + sigBlockSz;
        ret = PreparePacket(ssh, payloadSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = msgId;

        /* add host public key */
        switch (sigKeyBlock_ptr->pubKeyId) {
            case ID_SSH_RSA:
            {
#ifndef WOLFSSH_NO_SSH_RSA_SHA1
            /* Copy the rsaKeyBlock into the buffer. */
            c32toa(sigKeyBlock_ptr->sz, output + idx);
            idx += LENGTH_SZ;
            c32toa(sigKeyBlock_ptr->nameSz, output + idx);
            idx += LENGTH_SZ;
            WMEMCPY(output + idx, sigKeyBlock_ptr->name, sigKeyBlock_ptr->nameSz);
            idx += sigKeyBlock_ptr->nameSz;

            c32toa(sigKeyBlock_ptr->sk.rsa.eSz + sigKeyBlock_ptr->sk.rsa.ePad,
                   output + idx);
            idx += LENGTH_SZ;
            if (sigKeyBlock_ptr->sk.rsa.ePad) output[idx++] = 0;
            WMEMCPY(output + idx, sigKeyBlock_ptr->sk.rsa.e, sigKeyBlock_ptr->sk.rsa.eSz);
            idx += sigKeyBlock_ptr->sk.rsa.eSz;
            c32toa(sigKeyBlock_ptr->sk.rsa.nSz + sigKeyBlock_ptr->sk.rsa.nPad,
                   output + idx);
            idx += LENGTH_SZ;
            if (sigKeyBlock_ptr->sk.rsa.nPad) output[idx++] = 0;
            WMEMCPY(output + idx, sigKeyBlock_ptr->sk.rsa.n, sigKeyBlock_ptr->sk.rsa.nSz);
            idx += sigKeyBlock_ptr->sk.rsa.nSz;
#endif
            }
            break;

            case ID_ECDSA_SHA2_NISTP256:
            case ID_ECDSA_SHA2_NISTP384:
            case ID_ECDSA_SHA2_NISTP521:
            {
#ifndef WOLFSSH_NO_ECDSA
            /* Copy the rsaKeyBlock into the buffer. */
            c32toa(sigKeyBlock_ptr->sz, output + idx);
            idx += LENGTH_SZ;
            c32toa(sigKeyBlock_ptr->nameSz, output + idx);
            idx += LENGTH_SZ;
            WMEMCPY(output + idx, sigKeyBlock_ptr->name, sigKeyBlock_ptr->nameSz);
            idx += sigKeyBlock_ptr->nameSz;

            c32toa(sigKeyBlock_ptr->sk.ecc.primeNameSz, output + idx);
            idx += LENGTH_SZ;
            WMEMCPY(output + idx, sigKeyBlock_ptr->sk.ecc.primeName,
                    sigKeyBlock_ptr->sk.ecc.primeNameSz);
            idx += sigKeyBlock_ptr->sk.ecc.primeNameSz;
            c32toa(sigKeyBlock_ptr->sk.ecc.qSz, output + idx);
            idx += LENGTH_SZ;
            WMEMCPY(output + idx, sigKeyBlock_ptr->sk.ecc.q,
                    sigKeyBlock_ptr->sk.ecc.qSz);
            idx += sigKeyBlock_ptr->sk.ecc.qSz;
#endif
            }
            break;

        #ifdef WOLFSSH_CERTS
            case ID_X509V3_SSH_RSA:
            case ID_X509V3_ECDSA_SHA2_NISTP256:
            case ID_X509V3_ECDSA_SHA2_NISTP384:
            case ID_X509V3_ECDSA_SHA2_NISTP521:
            {
                ret = BuildRFC6187Info(ssh, sigKeyBlock_ptr->pubKeyId,
                    ssh->ctx->cert[keyIdx], ssh->ctx->certSz[keyIdx], NULL, 0,
                    output, &ssh->outputBuffer.bufferSz, &idx);
            }
            break;
        #endif
        }
    }

    if (ret == WS_SUCCESS) {
        /* Copy the server's public key. F for DE, or Q_S for ECDH. */
        c32toa(fSz + fPad, output + idx);
        idx += LENGTH_SZ;
        if (fPad) output[idx++] = 0;
        WMEMCPY(output + idx, f_ptr, fSz);
        idx += fSz;

        /* Copy the signature of the exchange hash. */
        c32toa(sigBlockSz, output + idx);
        idx += LENGTH_SZ;
        c32toa(sigKeyBlock_ptr->nameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, sigKeyBlock_ptr->name, sigKeyBlock_ptr->nameSz);
        idx += sigKeyBlock_ptr->nameSz;
        c32toa(sigSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, sig_ptr, sigSz);
        idx += sigSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = SendNewKeys(ssh);

    if (ret != WS_WANT_WRITE && ret != WS_SUCCESS)
        PurgePacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendKexDhReply(), ret = %d", ret);
    if (sigKeyBlock_ptr)
        WFREE(sigKeyBlock_ptr, heap, DYNTYPE_PRIVKEY);
#ifdef WOLFSSH_SMALL_STACK
    if (f_ptr)
        WFREE(f_ptr, heap, DYNTYPE_BUFFER);
    if (sig_ptr)
        WFREE(sig_ptr, heap, DYNTYPE_BUFFER);
#ifndef WOLFSSH_NO_DH
    if (y_ptr)
        WFREE(y_ptr, heap, DYNTYPE_PRIVKEY);
#endif
    if (r_ptr)
        WFREE(r_ptr, heap, DYNTYPE_BUFFER);
    if (s_ptr)
        WFREE(s_ptr, heap, DYNTYPE_BUFFER);
#endif
    return ret;
}


int SendNewKeys(WOLFSSH* ssh)
{
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendNewKeys()");
    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_NEWKEYS;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS) {
        ssh->blockSz = ssh->handshake->blockSz;
        ssh->encryptId = ssh->handshake->encryptId;
        ssh->macSz = ssh->handshake->macSz;
        ssh->macId = ssh->handshake->macId;
        ssh->aeadMode = ssh->handshake->aeadMode;
        WMEMCPY(&ssh->keys, &ssh->handshake->keys, sizeof(Keys));

        switch (ssh->encryptId) {
            case ID_NONE:
                WLOG(WS_LOG_DEBUG, "SNK: using cipher none");
                break;

#ifndef WOLFSSH_NO_AES_CBC
            case ID_AES128_CBC:
            case ID_AES192_CBC:
            case ID_AES256_CBC:
                WLOG(WS_LOG_DEBUG, "SNK: using cipher aes-cbc");
                ret = wc_AesSetKey(&ssh->encryptCipher.aes,
                                  ssh->keys.encKey, ssh->keys.encKeySz,
                                  ssh->keys.iv, AES_ENCRYPTION);
                break;
#endif

#ifndef WOLFSSH_NO_AES_CTR
            case ID_AES128_CTR:
            case ID_AES192_CTR:
            case ID_AES256_CTR:
                WLOG(WS_LOG_DEBUG, "SNK: using cipher aes-ctr");
                ret = wc_AesSetKey(&ssh->encryptCipher.aes,
                                  ssh->keys.encKey, ssh->keys.encKeySz,
                                  ssh->keys.iv, AES_ENCRYPTION);
                break;
#endif

#ifndef WOLFSSH_NO_AES_GCM
            case ID_AES128_GCM:
            case ID_AES192_GCM:
            case ID_AES256_GCM:
                WLOG(WS_LOG_DEBUG, "SNK: using cipher aes-gcm");
                ret = wc_AesGcmSetKey(&ssh->encryptCipher.aes,
                                     ssh->keys.encKey, ssh->keys.encKeySz);
                break;
#endif

            default:
                WLOG(WS_LOG_DEBUG, "SNK: using cipher invalid");
                ret = WS_INVALID_ALGO_ID;
        }
    }

    if (ret == WS_SUCCESS) {
        ssh->txCount = 0;
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendNewKeys(), ret = %d", ret);
    return ret;
}


#ifndef WOLFSSH_NO_DH_GEX_SHA256
int SendKexDhGexRequest(WOLFSSH* ssh)
{
    byte* output;
    word32 idx = 0;
    word32 payloadSz;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendKexDhGexRequest()");
    if (ssh == NULL || ssh->handshake == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        payloadSz = MSG_ID_SZ + (UINT32_SZ * 3);
        ret = PreparePacket(ssh, payloadSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_KEXDH_GEX_REQUEST;

        WLOG(WS_LOG_INFO, "  min = %u, preferred = %u, max = %u",
                ssh->handshake->dhGexMinSz,
                ssh->handshake->dhGexPreferredSz,
                ssh->handshake->dhGexMaxSz);
        c32toa(ssh->handshake->dhGexMinSz, output + idx);
        idx += UINT32_SZ;
        c32toa(ssh->handshake->dhGexPreferredSz, output + idx);
        idx += UINT32_SZ;
        c32toa(ssh->handshake->dhGexMaxSz, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendKexDhGexRequest(), ret = %d", ret);
    return ret;
}


int SendKexDhGexGroup(WOLFSSH* ssh)
{
    byte* output;
    word32 idx = 0;
    word32 payloadSz;
    const byte* primeGroup = dhPrimeGroup14;
    word32 primeGroupSz = dhPrimeGroup14Sz;
    const byte* generator = dhGenerator;
    word32 generatorSz = dhGeneratorSz;
    byte primePad = 0;
    byte generatorPad = 0;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendKexDhGexGroup()");
    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (primeGroup[0] & 0x80)
        primePad = 1;

    if (generator[0] & 0x80)
        generatorPad = 1;

    if (ret == WS_SUCCESS) {
        payloadSz = MSG_ID_SZ + (LENGTH_SZ * 2) +
                    primeGroupSz + primePad +
                    generatorSz + generatorPad;
        ret = PreparePacket(ssh, payloadSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_KEXDH_GEX_GROUP;

        c32toa(primeGroupSz + primePad, output + idx);
        idx += LENGTH_SZ;

        if (primePad) {
            output[idx] = 0;
            idx += 1;
        }

        WMEMCPY(output + idx, primeGroup, primeGroupSz);
        idx += primeGroupSz;

        c32toa(generatorSz + generatorPad, output + idx);
        idx += LENGTH_SZ;

        if (generatorPad) {
            output[idx] = 0;
            idx += 1;
        }

        WMEMCPY(output + idx, generator, generatorSz);
        idx += generatorSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendKexDhGexGroup(), ret = %d", ret);
    return ret;
}
#endif


int SendKexDhInit(WOLFSSH* ssh)
{
    byte* output;
    word32 idx = 0;
    word32 payloadSz;
#ifndef WOLFSSH_NO_DH
    const byte* primeGroup = NULL;
    word32 primeGroupSz = 0;
    const byte* generator = NULL;
    word32 generatorSz = 0;
#endif
    int ret = WS_SUCCESS;
    byte msgId = MSGID_KEXDH_INIT;
    byte e[MAX_KEX_KEY_SZ+1]; /* plus 1 in case of padding. */
    word32 eSz = sizeof(e);
    byte  ePad = 0;

    WLOG(WS_LOG_DEBUG, "Entering SendKexDhInit()");

    switch (ssh->handshake->kexId) {
#ifndef WOLFSSH_NO_DH_GROUP1_SHA1
        case ID_DH_GROUP1_SHA1:
            primeGroup = dhPrimeGroup1;
            primeGroupSz = dhPrimeGroup1Sz;
            generator = dhGenerator;
            generatorSz = dhGeneratorSz;
            break;
#endif
#ifndef WOLFSSH_NO_DH_GROUP14_SHA1
        case ID_DH_GROUP14_SHA1:
            primeGroup = dhPrimeGroup14;
            primeGroupSz = dhPrimeGroup14Sz;
            generator = dhGenerator;
            generatorSz = dhGeneratorSz;
            break;
#endif
#ifndef WOLFSSH_NO_DH_GEX_SHA256
        case ID_DH_GEX_SHA256:
            primeGroup = ssh->handshake->primeGroup;
            primeGroupSz = ssh->handshake->primeGroupSz;
            generator = ssh->handshake->generator;
            generatorSz = ssh->handshake->generatorSz;
            msgId = MSGID_KEXDH_GEX_INIT;
            break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256
        case ID_ECDH_SHA2_NISTP256:
            ssh->handshake->useEcc = 1;
            msgId = MSGID_KEXECDH_INIT;
            break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP384
        case ID_ECDH_SHA2_NISTP384:
            ssh->handshake->useEcc = 1;
            msgId = MSGID_KEXECDH_INIT;
            break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP521
        case ID_ECDH_SHA2_NISTP521:
            ssh->handshake->useEcc = 1;
            msgId = MSGID_KEXECDH_INIT;
            break;
#endif
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
        case ID_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256:
            /* Only support level 1 for now. */
            ssh->handshake->useEccKyber = 1;
            msgId = MSGID_KEXKEM_INIT;
            break;
#endif
        default:
            WLOG(WS_LOG_DEBUG, "Invalid algo: %u", ssh->handshake->kexId);
            ret = WS_INVALID_ALGO_ID;
    }


    if (ret == WS_SUCCESS) {
        if (!ssh->handshake->useEcc
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
            && !ssh->handshake->useEccKyber
#endif
) {
#ifndef WOLFSSH_NO_DH
            DhKey* privKey = &ssh->handshake->privKey.dh;

            ret = wc_InitDhKey(privKey);
            if (ret == 0)
                ret = wc_DhSetKey(privKey, primeGroup, primeGroupSz,
                                  generator, generatorSz);
            if (ret == 0)
                ret = wc_DhGenerateKeyPair(privKey, ssh->rng,
                                           ssh->handshake->x,
                                           &ssh->handshake->xSz,
                                           e, &eSz);
#endif
        }
        else if (ssh->handshake->useEcc
#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
                 || ssh->handshake->useEccKyber
#endif
                ) {
#if !defined(WOLFSSH_NO_ECDH)
            ecc_key* privKey = &ssh->handshake->privKey.ecc;
            int primeId = wcPrimeForId(ssh->handshake->kexId);

            if (primeId == ECC_CURVE_INVALID)
                ret = WS_INVALID_PRIME_CURVE;

            if (ret == 0)
                ret = wc_ecc_init_ex(privKey, ssh->ctx->heap,
                                     INVALID_DEVID);
#ifdef HAVE_WC_ECC_SET_RNG
            if (ret == 0)
                ret = wc_ecc_set_rng(privKey, ssh->rng);
#endif
            if (ret == 0)
                ret = wc_ecc_make_key_ex(ssh->rng,
                                     wc_ecc_get_curve_size_from_id(primeId),
                                     privKey, primeId);
            if (ret == 0) {
                PRIVATE_KEY_UNLOCK();
                ret = wc_ecc_export_x963(privKey, e, &eSz);
                PRIVATE_KEY_LOCK();
            }
#else
            ret = WS_INVALID_ALGO_ID;
#endif /* !defined(WOLFSSH_NO_ECDH) */
        }
        else {
            ret = WS_INVALID_ALGO_ID;
        }

#ifndef WOLFSSH_NO_ECDH_SHA2_NISTP256_KYBER_LEVEL1_SHA256
        if (ssh->handshake->useEccKyber) {
            OQS_KEM* kem = NULL;
            ret = 0;

            kem = OQS_KEM_new(OQS_KEM_alg_kyber_512);
            if (kem == NULL) {
                ret = WS_INVALID_ALGO_ID;
            }

            if (ret == 0) {
                if (OQS_KEM_keypair(kem, e + eSz, ssh->handshake->x)
                    != OQS_SUCCESS) {
                    /* This should never happen */
                    ret = WS_ERROR;
                }
                eSz += kem->length_public_key;
                ssh->handshake->xSz = (word32)kem->length_secret_key;
            }

            if (kem != NULL) {
                OQS_KEM_free(kem);
            }
        }
#endif

        if (ret == 0)
            ret = WS_SUCCESS;
    }

    if (ret == WS_SUCCESS) {
        ret = CreateMpint(e, &eSz, &ePad);
    }

    if (ret == WS_SUCCESS) {
        if (ePad == 1) {
            ssh->handshake->e[0] = 0;
        }
        WMEMCPY(ssh->handshake->e + ePad, e, eSz);
        ssh->handshake->eSz = eSz + ePad;

        payloadSz = MSG_ID_SZ + LENGTH_SZ + eSz + ePad;
        ret = PreparePacket(ssh, payloadSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = msgId;

        c32toa(eSz + ePad, output + idx);
        idx += LENGTH_SZ;

        if (ePad) {
            output[idx] = 0;
            idx++;
        }

        WMEMCPY(output + idx, e, eSz);
        idx += eSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendKexDhInit(), ret = %d", ret);
    return ret;
}


int SendUnimplemented(WOLFSSH* ssh)
{
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL) {
        WLOG(WS_LOG_DEBUG, "Entering SendUnimplemented(), no parameter");
        ret = WS_BAD_ARGUMENT;
        WLOG(WS_LOG_DEBUG, "Leaving SendUnimplemented(), ret = %d", ret);
        return ret;
    }

    WLOG(WS_LOG_DEBUG,
         "Entering SendUnimplemented(), peerSeq = %u", ssh->peerSeq);

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + LENGTH_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_UNIMPLEMENTED;
        c32toa(ssh->peerSeq, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendUnimplemented(), ret = %d", ret);
    return ret;
}


int SendDisconnect(WOLFSSH* ssh, word32 reason)
{
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + (LENGTH_SZ * 2));

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_DISCONNECT;
        c32toa(reason, output + idx);
        idx += UINT32_SZ;
        c32toa(0, output + idx);
        idx += LENGTH_SZ;
        c32toa(0, output + idx);
        idx += LENGTH_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}


int SendIgnore(WOLFSSH* ssh, const unsigned char* data, word32 dataSz)
{
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL || (data == NULL && dataSz > 0))
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + LENGTH_SZ + dataSz);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_IGNORE;
        c32toa(dataSz, output + idx);
        idx += LENGTH_SZ;
        if (dataSz > 0) {
            WMEMCPY(output + idx, data, dataSz);
            idx += dataSz;
        }

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}

int SendGlobalRequest(WOLFSSH* ssh, const unsigned char* data, word32 dataSz, int reply)
{
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL || (data == NULL && dataSz > 0))
        ret = WS_BAD_ARGUMENT;

    WLOG(WS_LOG_DEBUG, "Enter SendGlobalRequest");

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + LENGTH_SZ + dataSz + BOOLEAN_SZ);
    WLOG(WS_LOG_DEBUG, "Done PreparePacket");

    if (ret == WS_SUCCESS)
    {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_GLOBAL_REQUEST;
        c32toa(dataSz, output + idx);
        idx += LENGTH_SZ;
        if (dataSz > 0)
        {
            WMEMCPY(output + idx, data, dataSz);
            idx += dataSz;
        }

        output[idx++] = reply;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }
    WLOG(WS_LOG_DEBUG, "Done BundlePacket");

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendServiceRequest(), ret = %d", ret);

    return ret;
}

static const char cannedLangTag[] = "en-us";
static const word32 cannedLangTagSz = sizeof(cannedLangTag) - 1;


int SendDebug(WOLFSSH* ssh, byte alwaysDisplay, const char* msg)
{
    word32 msgSz;
    byte* output;
    word32 idx = 0;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        msgSz = (msg != NULL) ? (word32)WSTRLEN(msg) : 0;

        ret = PreparePacket(ssh,
                            MSG_ID_SZ + BOOLEAN_SZ + (LENGTH_SZ * 2) +
                            msgSz + cannedLangTagSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_DEBUG;
        output[idx++] = (alwaysDisplay != 0);
        c32toa(msgSz, output + idx);
        idx += LENGTH_SZ;
        if (msgSz > 0) {
            WMEMCPY(output + idx, msg, msgSz);
            idx += msgSz;
        }
        c32toa(cannedLangTagSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, cannedLangTag, cannedLangTagSz);
        idx += cannedLangTagSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}


int SendServiceRequest(WOLFSSH* ssh, byte serviceId)
{
    const char* serviceName;
    word32 serviceNameSz;
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendServiceRequest()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        serviceName = IdToName(serviceId);
        serviceNameSz = (word32)WSTRLEN(serviceName);

        ret = PreparePacket(ssh,
                            MSG_ID_SZ + LENGTH_SZ + serviceNameSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_SERVICE_REQUEST;
        c32toa(serviceNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, serviceName, serviceNameSz);
        idx += serviceNameSz;

        ssh->outputBuffer.length = idx;
        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendServiceRequest(), ret = %d", ret);
    return ret;
}


int SendServiceAccept(WOLFSSH* ssh, byte serviceId)
{
    const char* serviceName;
    word32 serviceNameSz;
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        serviceName = IdToName(serviceId);
        serviceNameSz = (word32)WSTRLEN(serviceName);
        ret = PreparePacket(ssh, MSG_ID_SZ + LENGTH_SZ + serviceNameSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_SERVICE_ACCEPT;
        c32toa(serviceNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, serviceName, serviceNameSz);
        idx += serviceNameSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = SendUserAuthBanner(ssh);

    return ret;
}


typedef struct WS_KeySignature {
    byte keySigId;
    word32 sigSz;
    const char *name;
    word32 nameSz;
    union {
#ifndef WOLFSSH_NO_RSA
        struct {
            RsaKey key;
            byte e[256];
            word32 eSz;
            byte ePad;
            byte n[256];
            word32 nSz;
            byte nPad;
        } rsa;
#endif
#ifndef WOLFSSH_NO_ECDSA
        struct {
            ecc_key key;
            word32 keyBlobSz;
            const char *keyBlobName;
            word32 keyBlobNameSz;
            byte q[256];
            word32 qSz;
            byte qPad;
            const char *primeName;
            word32 primeNameSz;
        } ecc;
#endif
    } ks;
} WS_KeySignature;


/* Updates the payload size, and maybe loads keys. */
static int PrepareUserAuthRequestPassword(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || payloadSz == NULL || authData == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        *payloadSz += BOOLEAN_SZ + LENGTH_SZ +
                authData->sf.password.passwordSz;

    return ret;
}


static int BuildUserAuthRequestPassword(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData)
{
    int ret = WS_SUCCESS;
    word32 begin;

    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        output[begin++] = 0; /* Boolean "FALSE" for password change */
        c32toa(authData->sf.password.passwordSz, output + begin);
        begin += LENGTH_SZ;
        WMEMCPY(output + begin, authData->sf.password.password,
                authData->sf.password.passwordSz);
        begin += authData->sf.password.passwordSz;
        *idx = begin;
    }

    return ret;
}


#ifndef WOLFSSH_NO_RSA
static int PrepareUserAuthRequestRsa(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData, WS_KeySignature* keySig)
{
    int ret = WS_SUCCESS;

    if (ssh == NULL || payloadSz == NULL || authData == NULL || keySig == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = wc_InitRsaKey(&keySig->ks.rsa.key, NULL);

    if (ret == WS_SUCCESS) {
        word32 idx = 0;
        #ifdef WOLFSSH_AGENT
        if (ssh->agentEnabled)
            ret = wc_RsaPublicKeyDecode(authData->sf.publicKey.publicKey,
                    &idx, &keySig->ks.rsa.key,
                    authData->sf.publicKey.publicKeySz);
        else
        #endif
            ret = wc_RsaPrivateKeyDecode(authData->sf.publicKey.privateKey,
                    &idx, &keySig->ks.rsa.key,
                    authData->sf.publicKey.privateKeySz);
    }

    if (ret == WS_SUCCESS) {
        if (authData->sf.publicKey.hasSignature) {
            int sigSz = wc_RsaEncryptSize(&keySig->ks.rsa.key);

            if (sigSz >= 0) {
                *payloadSz += (LENGTH_SZ * 3) + (word32)sigSz +
                        authData->sf.publicKey.publicKeyTypeSz;
                keySig->sigSz = sigSz;
            }
            else
                ret = sigSz;
        }
    }

    return ret;
}


static int BuildUserAuthRequestRsa(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData,
        const byte* sigStart, word32 sigStartIdx,
        WS_KeySignature* keySig)
{
    wc_HashAlg hash;
    byte digest[WC_MAX_DIGEST_SIZE];
    word32 digestSz = 0;
    word32 begin;
    enum wc_HashType hashId = WC_HASH_TYPE_SHA;
    int ret = WS_SUCCESS;
    byte* checkData = NULL;
    word32 checkDataSz = 0;

    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL ||
            sigStart == NULL || keySig == NULL) {
        ret = WS_BAD_ARGUMENT;
        return ret;
    }

    begin = *idx;

    if (ret == WS_SUCCESS) {
        hashId = HashForId(keySig->keySigId);
        if (hashId == WC_HASH_TYPE_NONE)
            ret = WS_INVALID_ALGO_ID;
    }
    if (ret == WS_SUCCESS) {
        int checkSz = wc_HashGetDigestSize(hashId);
        if (checkSz > 0)
            digestSz = (word32)checkSz;
        else
            ret = WS_INVALID_ALGO_ID;
    }
    if (ret == WS_SUCCESS) {
        checkDataSz = LENGTH_SZ + ssh->sessionIdSz + (begin - sigStartIdx);
        checkData = (byte*)WMALLOC(checkDataSz, ssh->ctx->heap, DYNTYPE_TEMP);
        if (checkData == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        word32 i = 0;

        c32toa(ssh->sessionIdSz, checkData + i);
        i += LENGTH_SZ;
        WMEMCPY(checkData + i, ssh->sessionId, ssh->sessionIdSz);
        i += ssh->sessionIdSz;
        WMEMCPY(checkData + i, sigStart, begin - sigStartIdx);
    }

    #ifdef WOLFSSH_AGENT
    if (ssh->agentEnabled) {
        if (ret == WS_SUCCESS)
            ret = wolfSSH_AGENT_SignRequest(ssh, checkData, checkDataSz,
                    output + begin + LENGTH_SZ, &keySig->sigSz,
                    authData->sf.publicKey.publicKey,
                    authData->sf.publicKey.publicKeySz, 0);
        if (ret == WS_SUCCESS) {
            c32toa(keySig->sigSz, output + begin);
            begin += LENGTH_SZ + keySig->sigSz;
        }
    }
    else
    #endif
    {
        if (ret == WS_SUCCESS) {
            byte encDigest[MAX_ENCODED_SIG_SZ];
            int encDigestSz;

            WMEMSET(digest, 0, sizeof(digest));
            ret = wc_HashInit(&hash, hashId);
            if (ret == WS_SUCCESS)
                ret = HashUpdate(&hash, hashId, checkData, checkDataSz);
            if (ret == WS_SUCCESS)
                ret = wc_HashFinal(&hash, hashId, digest);

            c32toa(keySig->sigSz + 7 + LENGTH_SZ * 2, output + begin);
            begin += LENGTH_SZ;
            c32toa(7, output + begin);
            begin += LENGTH_SZ;
            WMEMCPY(output + begin, "ssh-rsa", 7);
            begin += 7;
            c32toa(keySig->sigSz, output + begin);
            begin += LENGTH_SZ;
            encDigestSz = wc_EncodeSignature(encDigest, digest, digestSz,
                    wc_HashGetOID(hashId));
            if (encDigestSz <= 0) {
                WLOG(WS_LOG_DEBUG, "SUAR: Bad Encode Sig");
                ret = WS_CRYPTO_FAILED;
            }
            else {
                int sigSz;
                WLOG(WS_LOG_INFO, "Signing hash with RSA.");
                sigSz = wc_RsaSSL_Sign(encDigest, encDigestSz,
                        output + begin, keySig->sigSz,
                        &keySig->ks.rsa.key, ssh->rng);
                if (sigSz <= 0 || (word32)sigSz != keySig->sigSz) {
                    WLOG(WS_LOG_DEBUG, "SUAR: Bad RSA Sign");
                    ret = WS_RSA_E;
                }
            }

            if (ret == WS_SUCCESS)
                begin += keySig->sigSz;
        }
    }

    if (ret == WS_SUCCESS)
        *idx = begin;

    if (checkData != NULL) {
        ForceZero(checkData, checkDataSz);
        WFREE(checkData, ssh->ctx->heap, DYNTYPE_TEMP);
    }

    return ret;
}


#ifdef WOLFSSH_CERTS
static int PrepareUserAuthRequestRsaCert(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData, WS_KeySignature* keySig)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering PrepareUserAuthRequestRsaCert()");
    if (ssh == NULL || payloadSz == NULL || authData == NULL || keySig == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = wc_InitRsaKey(&keySig->ks.rsa.key, NULL);

    if (ret == WS_SUCCESS) {
        word32 idx = 0;
        #ifdef WOLFSSH_AGENT
        if (ssh->agentEnabled)
            ret = wc_RsaPublicKeyDecode(authData->sf.publicKey.publicKey,
                    &idx, &keySig->ks.rsa.key,
                    authData->sf.publicKey.publicKeySz);
        else
        #endif /* WOLFSSH_AGENT */
            ret = wc_RsaPrivateKeyDecode(authData->sf.publicKey.privateKey,
                    &idx, &keySig->ks.rsa.key,
                    authData->sf.publicKey.privateKeySz);
    }

    if (ret == WS_SUCCESS) {
        *payloadSz += (LENGTH_SZ + authData->sf.publicKey.publicKeyTypeSz) +
                (UINT32_SZ * 2); /* certificate and ocsp counts */

        if (authData->sf.publicKey.hasSignature) {
            int sigSz = wc_RsaEncryptSize(&keySig->ks.rsa.key);

            if (sigSz >= 0) {
                *payloadSz += (LENGTH_SZ * 3) + (word32)sigSz +
                        authData->sf.publicKey.publicKeyTypeSz;
                keySig->sigSz = sigSz;
            }
            else
                ret = sigSz;
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving PrepareUserAuthRequestRsaCert(), ret = %d",
            ret);
    return ret;
}


static int BuildUserAuthRequestRsaCert(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData,
        const byte* sigStart, word32 sigStartIdx,
        WS_KeySignature* keySig)
{
    wc_HashAlg hash;
    byte digest[WC_MAX_DIGEST_SIZE];
    word32 digestSz = 0;
    word32 begin;
    enum wc_HashType hashId = WC_HASH_TYPE_SHA;
    int ret = WS_SUCCESS;
    byte* checkData = NULL;
    word32 checkDataSz = 0;

    WLOG(WS_LOG_DEBUG, "Entering BuildUserAuthRequestRsaCert()");
    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL ||
            sigStart == NULL || keySig == NULL) {
        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS) {
        begin = *idx;
        hashId = HashForId(keySig->keySigId);
        if (hashId == WC_HASH_TYPE_NONE)
            ret = WS_INVALID_ALGO_ID;
        WLOG(WS_LOG_DEBUG, "HashForId = %d, ret = %d", hashId, ret);
    }
    if (ret == WS_SUCCESS) {
        int checkSz = wc_HashGetDigestSize(hashId);
        if (checkSz > 0)
            digestSz = (word32)checkSz;
        else
            ret = WS_INVALID_ALGO_ID;
        WLOG(WS_LOG_DEBUG, "HashGetDigestSz = %d, ret = %d", checkSz, ret);
    }
    if (ret == WS_SUCCESS) {
        checkDataSz = LENGTH_SZ + ssh->sessionIdSz + (begin - sigStartIdx);
        checkData = (byte*)WMALLOC(checkDataSz, ssh->ctx->heap, DYNTYPE_TEMP);
        if (checkData == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        word32 i = 0;

        c32toa(ssh->sessionIdSz, checkData + i);
        i += LENGTH_SZ;
        WMEMCPY(checkData + i, ssh->sessionId, ssh->sessionIdSz);
        i += ssh->sessionIdSz;
        WMEMCPY(checkData + i, sigStart, begin - sigStartIdx);
    }

    if (ret == WS_SUCCESS) {
        #ifdef WOLFSSH_AGENT
        if (ssh->agentEnabled) {
            if (ret == WS_SUCCESS)
                ret = wolfSSH_AGENT_SignRequest(ssh, checkData, checkDataSz,
                        output + begin + LENGTH_SZ, &keySig->sigSz,
                        authData->sf.publicKey.publicKey,
                        authData->sf.publicKey.publicKeySz, 0);
            if (ret == WS_SUCCESS) {
                c32toa(keySig->sigSz, output + begin);
                begin += LENGTH_SZ + keySig->sigSz;
            }
        }
        else
        #endif /* WOLFSSH_AGENT */
        {
            byte encDigest[MAX_ENCODED_SIG_SZ];
            int encDigestSz;

            WMEMSET(digest, 0, sizeof(digest));
            ret = wc_HashInit(&hash, hashId);
            if (ret == WS_SUCCESS)
                ret = HashUpdate(&hash, hashId, checkData, checkDataSz);
            if (ret == WS_SUCCESS)
                ret = wc_HashFinal(&hash, hashId, digest);

            c32toa(keySig->sigSz + 7 + LENGTH_SZ * 2, output + begin);
            begin += LENGTH_SZ;
            c32toa(7, output + begin);
            begin += LENGTH_SZ;
            WMEMCPY(output + begin, "ssh-rsa", 7);
            begin += 7;
            c32toa(keySig->sigSz, output + begin);
            begin += LENGTH_SZ;
            encDigestSz = wc_EncodeSignature(encDigest, digest, digestSz,
                    wc_HashGetOID(hashId));
            if (encDigestSz <= 0) {
                WLOG(WS_LOG_DEBUG, "SUAR: Bad Encode Sig");
                ret = WS_CRYPTO_FAILED;
            }
            else {
                int sigSz;
                WLOG(WS_LOG_INFO, "Signing hash with RSA.");
                sigSz = wc_RsaSSL_Sign(encDigest, encDigestSz,
                        output + begin, keySig->sigSz,
                        &keySig->ks.rsa.key, ssh->rng);
                if (sigSz <= 0 || (word32)sigSz != keySig->sigSz) {
                    WLOG(WS_LOG_DEBUG, "SUAR: Bad RSA Sign");
                    ret = WS_RSA_E;
                }
            }

            if (ret == WS_SUCCESS)
                begin += keySig->sigSz;
        }
    }

    if (ret == WS_SUCCESS)
        *idx = begin;

    if (checkData != NULL) {
        ForceZero(checkData, checkDataSz);
        WFREE(checkData, ssh->ctx->heap, DYNTYPE_TEMP);
    }

    WLOG(WS_LOG_DEBUG, "Leaving BuildUserAuthRequestRsaCert(), ret = %d",
            ret);
    return ret;
}
#endif /* WOLFSSH_CERTS */
#endif /* ! WOLFSSH_NO_RSA */


#ifndef WOLFSSH_NO_ECDSA
static int PrepareUserAuthRequestEcc(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData, WS_KeySignature* keySig)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering PrepareUserAuthRequestEcc()");
    if (ssh == NULL || payloadSz == NULL || authData == NULL || keySig == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = wc_ecc_init(&keySig->ks.ecc.key);

    if (ret == 0) {
        word32 idx = 0;
        #ifdef WOLFSSH_AGENT
        if (ssh->agentEnabled) {
            word32 sz;
            const byte* c = (const byte*)authData->sf.publicKey.publicKey;

            ato32(c + idx, &sz);
            idx += LENGTH_SZ + sz;
            ato32(c + idx, &sz);
            idx += LENGTH_SZ + sz;
            ato32(c + idx, &sz);
            idx += LENGTH_SZ;
            c += idx;
            idx = 0;

            ret = wc_ecc_import_x963(c, sz, &keySig->ks.ecc.key);
        }
        else
        #endif
            ret = wc_EccPrivateKeyDecode(authData->sf.publicKey.privateKey,
                    &idx, &keySig->ks.ecc.key,
                    authData->sf.publicKey.privateKeySz);
    }

    if (ret == WS_SUCCESS) {
        if (authData->sf.publicKey.hasSignature) {
            int sigSz = wc_ecc_sig_size(&keySig->ks.ecc.key);

            if (sigSz >= 0) {
                *payloadSz += (LENGTH_SZ * 5) + (word32)sigSz +
                        authData->sf.publicKey.publicKeyTypeSz;
                keySig->sigSz = sigSz;
            }
            else
                ret = sigSz;
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving PrepareUserAuthRequestEcc(), ret = %d", ret);
    return ret;
}


static int BuildUserAuthRequestEcc(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData,
        const byte* sigStart, word32 sigStartIdx,
        WS_KeySignature* keySig)
{
    wc_HashAlg hash;
    byte digest[WC_MAX_DIGEST_SIZE];
    word32 digestSz;
    word32 begin;
    enum wc_HashType hashId = WC_HASH_TYPE_SHA;
    int ret = WS_SUCCESS;
    byte* r_ptr;
    byte* s_ptr;
    byte* sig_ptr;
    word32 rSz = ECC_MAX_SIG_SIZE / 2;
    word32 sSz = ECC_MAX_SIG_SIZE / 2;
    word32 sigSz = ECC_MAX_SIG_SIZE;
    byte* checkData = NULL;
    word32 checkDataSz = 0;

#ifdef WOLFSSH_SMALL_STACK
    r_ptr = (byte*)WMALLOC(rSz, ssh->ctx->heap, DYNTYPE_BUFFER);
    s_ptr = (byte*)WMALLOC(sSz, ssh->ctx->heap, DYNTYPE_BUFFER);
    sig_ptr = (byte*)WMALLOC(sigSz, ssh->ctx->heap, DYNTYPE_BUFFER);
    if (r_ptr == NULL || s_ptr == NULL || sig_ptr == NULL)
        ret = WS_MEMORY_E;
#else
    byte r_s[ECC_MAX_SIG_SIZE / 2];
    byte s_s[ECC_MAX_SIG_SIZE / 2];
    byte sig_s[ECC_MAX_SIG_SIZE];
    r_ptr = r_s;
    s_ptr = s_s;
    sig_ptr = sig_s;
#endif

    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL ||
            sigStart == NULL || keySig == NULL) {
        ret = WS_BAD_ARGUMENT;
        return ret;
    }

    begin = *idx;

    if (ret == WS_SUCCESS) {
        hashId = HashForId(keySig->keySigId);
        WMEMSET(digest, 0, sizeof(digest));
        digestSz = wc_HashGetDigestSize(hashId);
        checkDataSz = LENGTH_SZ + ssh->sessionIdSz + (begin - sigStartIdx);
        checkData = (byte*)WMALLOC(checkDataSz, ssh->ctx->heap, DYNTYPE_TEMP);
        if (checkData == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        word32 i = 0;

        c32toa(ssh->sessionIdSz, checkData + i);
        i += LENGTH_SZ;
        WMEMCPY(checkData + i, ssh->sessionId, ssh->sessionIdSz);
        i += ssh->sessionIdSz;
        WMEMCPY(checkData + i, sigStart, begin - sigStartIdx);
    }

    #ifdef WOLFSSH_AGENT
    if (ssh->agentEnabled) {
        if (ret == WS_SUCCESS)
            ret = wolfSSH_AGENT_SignRequest(ssh, checkData, checkDataSz,
                    sig_ptr, &sigSz,
                    authData->sf.publicKey.publicKey,
                    authData->sf.publicKey.publicKeySz, 0);
        if (ret == WS_SUCCESS) {
            c32toa(sigSz, output + begin);
            begin += LENGTH_SZ;
            XMEMCPY(output + begin, sig_ptr, sigSz);
            begin += sigSz;
        }
    }
    else
    #endif
    {
        if (ret == WS_SUCCESS) {
            WLOG(WS_LOG_INFO, "Signing hash with ECDSA.");
            ret = wc_HashInit(&hash, hashId);
            if (ret == WS_SUCCESS)
                ret = HashUpdate(&hash, hashId, checkData, checkDataSz);
            if (ret == WS_SUCCESS)
                ret = wc_HashFinal(&hash, hashId, digest);
            if (ret == WS_SUCCESS)
                ret = wc_ecc_sign_hash(digest, digestSz, sig_ptr, &sigSz,
                        ssh->rng, &keySig->ks.ecc.key);
            if (ret != WS_SUCCESS) {
                WLOG(WS_LOG_DEBUG, "SUAR: Bad ECC Sign");
                ret = WS_ECC_E;
            }
        }

        if (ret == WS_SUCCESS) {
            ret = wc_ecc_sig_to_rs(sig_ptr, sigSz, r_ptr, &rSz, s_ptr, &sSz);
        }

        if (ret == WS_SUCCESS) {
            const char* names;
            word32 namesSz;
            byte rPad;
            byte sPad;

            /* adds a byte of padding if needed to avoid negative values */
            rPad = (r_ptr[0] & 0x80) ? 1 : 0;
            sPad = (s_ptr[0] & 0x80) ? 1 : 0;

            switch (keySig->keySigId) {
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
                case ID_ECDSA_SHA2_NISTP256:
                    names = cannedKeyAlgoEcc256Names;
                    namesSz = cannedKeyAlgoEcc256NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
                case ID_ECDSA_SHA2_NISTP384:
                    names = cannedKeyAlgoEcc384Names;
                    namesSz = cannedKeyAlgoEcc384NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
                case ID_ECDSA_SHA2_NISTP521:
                    names = cannedKeyAlgoEcc521Names;
                    namesSz = cannedKeyAlgoEcc521NamesSz;
                    break;
                #endif
                default:
                    WLOG(WS_LOG_DEBUG, "SUAR: ECDSA invalid algo");
                    ret = WS_INVALID_ALGO_ID;
            }

            if (ret == WS_SUCCESS) {
                c32toa(rSz + rPad + sSz + sPad + namesSz + LENGTH_SZ * 4,
                        output + begin);
                begin += LENGTH_SZ;

                c32toa(namesSz, output + begin);
                begin += LENGTH_SZ;

                WMEMCPY(output + begin, names, namesSz);
                begin += namesSz;

                c32toa(rSz + rPad + sSz + sPad + LENGTH_SZ * 2, output + begin);
                begin += LENGTH_SZ;

                c32toa(rSz + rPad, output + begin);
                begin += LENGTH_SZ;

                if (rPad)
                    output[begin++] = 0;

                WMEMCPY(output + begin, r_ptr, rSz);
                begin += rSz;

                c32toa(sSz + sPad, output + begin);
                begin += LENGTH_SZ;

                if (sPad)
                    output[begin++] = 0;

                WMEMCPY(output + begin, s_ptr, sSz);
                begin += sSz;
            }
        }
    }

    if (ret == WS_SUCCESS)
        *idx = begin;

    if (checkData != NULL) {
        ForceZero(checkData, checkDataSz);
        WFREE(checkData, ssh->ctx->heap, DYNTYPE_TEMP);
    }

#ifdef WOLFSSH_SMALL_STACK
    if (r_ptr)
        WFREE(r_ptr, ssh->ctx->heap, DYNTYPE_BUFFER);
    if (s_ptr)
        WFREE(s_ptr, ssh->ctx->heap, DYNTYPE_BUFFER);
    if (sig_ptr)
        WFREE(sig_ptr, ssh->ctx->heap, DYNTYPE_BUFFER);
#endif
    return ret;
}


#ifdef WOLFSSH_CERTS

static int PrepareUserAuthRequestEccCert(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData, WS_KeySignature* keySig)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering PrepareUserAuthRequestEccCert()");
    if (ssh == NULL || payloadSz == NULL || authData == NULL || keySig == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = wc_ecc_init(&keySig->ks.ecc.key);

    if (ret == WS_SUCCESS) {
        word32 idx = 0;
        #if 0
        #ifdef WOLFSSH_AGENT
        if (ssh->agentEnabled) {
            word32 sz;
            const byte* c = (const byte*)authData->sf.publicKey.publicKey;

            ato32(c + idx, &sz);
            idx += LENGTH_SZ + sz;
            ato32(c + idx, &sz);
            idx += LENGTH_SZ + sz;
            ato32(c + idx, &sz);
            idx += LENGTH_SZ;
            c += idx;
            idx = 0;

            ret = wc_ecc_import_x963(c, sz, &keySig->ks.ecc.key);
        }
        else
        #endif
        #endif
            ret = wc_EccPrivateKeyDecode(authData->sf.publicKey.privateKey,
                    &idx, &keySig->ks.ecc.key,
                    authData->sf.publicKey.privateKeySz);
    }

    if (ret == WS_SUCCESS) {
        *payloadSz += (LENGTH_SZ + authData->sf.publicKey.publicKeyTypeSz) +
                (UINT32_SZ * 2); /* certificate and ocsp counts */

        if (authData->sf.publicKey.hasSignature) {
            int sigSz = wc_ecc_sig_size(&keySig->ks.ecc.key);

            if (sigSz >= 0) {
                /* 5 lengths: sig(R), sig(S), sig, sig-type, sig-blob */
                *payloadSz += (LENGTH_SZ * 5) + (word32)sigSz +
                        authData->sf.publicKey.publicKeyTypeSz;
                keySig->sigSz = sigSz;
            }
            else
                ret = sigSz;
        }
    }

    WLOG(WS_LOG_DEBUG, "Leaving PrepareUserAuthRequestEccCert(), ret = %d",
            ret);
    return ret;
}


static int BuildUserAuthRequestEccCert(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData,
        const byte* sigStart, word32 sigStartIdx,
        WS_KeySignature* keySig)
{
    wc_HashAlg hash;
    byte digest[WC_MAX_DIGEST_SIZE];
    word32 digestSz;
    word32 begin;
    enum wc_HashType hashId = WC_HASH_TYPE_SHA;
    int ret = WS_SUCCESS;
    byte* r;
    byte* s;
    byte sig[139]; /* wc_ecc_sig_size() for a prime521 key. */
    byte rs[139];  /* wc_ecc_sig_size() for a prime521 key. */
    word32 sigSz = sizeof(sig), rSz, sSz;
    byte* checkData = NULL;
    word32 checkDataSz = 0;

    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL ||
            sigStart == NULL || keySig == NULL) {
        ret = WS_BAD_ARGUMENT;
        return ret;
    }

    begin = *idx;

    if (ret == WS_SUCCESS) {
        hashId = HashForId(keySig->keySigId);
        WMEMSET(digest, 0, sizeof(digest));
        digestSz = wc_HashGetDigestSize(hashId);
        checkDataSz = LENGTH_SZ + ssh->sessionIdSz + (begin - sigStartIdx);
        checkData = (byte*)WMALLOC(checkDataSz, ssh->ctx->heap, DYNTYPE_TEMP);
        if (checkData == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        word32 i = 0;

        c32toa(ssh->sessionIdSz, checkData + i);
        i += LENGTH_SZ;
        WMEMCPY(checkData + i, ssh->sessionId, ssh->sessionIdSz);
        i += ssh->sessionIdSz;
        WMEMCPY(checkData + i, sigStart, begin - sigStartIdx);
    }

    #if 0
    #ifdef WOLFSSH_AGENT
    if (ssh->agentEnabled) {
        if (ret == WS_SUCCESS)
            ret = wolfSSH_AGENT_SignRequest(ssh, checkData, checkDataSz,
                    sig, &sigSz,
                    authData->sf.publicKey.publicKey,
                    authData->sf.publicKey.publicKeySz, 0);
        if (ret == WS_SUCCESS) {
            c32toa(sigSz, output + begin);
            begin += LENGTH_SZ;
            XMEMCPY(output + begin, sig, sigSz);
            begin += sigSz;
        }
    }
    else
    #endif
    #endif
    {
        if (ret == WS_SUCCESS) {
            WLOG(WS_LOG_INFO, "Signing hash with ECDSA cert.");
            ret = wc_HashInit(&hash, hashId);
            if (ret == WS_SUCCESS)
                ret = HashUpdate(&hash, hashId, checkData, checkDataSz);
            if (ret == WS_SUCCESS)
                ret = wc_HashFinal(&hash, hashId, digest);
            if (ret == WS_SUCCESS)
                ret = wc_ecc_sign_hash(digest, digestSz, sig, &sigSz,
                        ssh->rng, &keySig->ks.ecc.key);
            if (ret != WS_SUCCESS) {
                WLOG(WS_LOG_DEBUG, "SUAR: Bad ECC Cert Sign");
                ret = WS_ECC_E;
            }
        }

        if (ret == WS_SUCCESS) {
            rSz = sSz = sizeof(rs) / 2;
            r = rs;
            s = rs + rSz;
            ret = wc_ecc_sig_to_rs(sig, sigSz, r, &rSz, s, &sSz);
        }

        if (ret == WS_SUCCESS) {
            const char* names;
            word32 namesSz;
            byte rPad;
            byte sPad;

            /* adds a byte of padding if needed to avoid negative values */
            rPad = (r[0] & 0x80) ? 1 : 0;
            sPad = (s[0] & 0x80) ? 1 : 0;

            switch (keySig->keySigId) {
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
                case ID_ECDSA_SHA2_NISTP256:
                    names = cannedKeyAlgoEcc256Names;
                    namesSz = cannedKeyAlgoEcc256NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
                case ID_ECDSA_SHA2_NISTP384:
                    names = cannedKeyAlgoEcc384Names;
                    namesSz = cannedKeyAlgoEcc384NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
                case ID_ECDSA_SHA2_NISTP521:
                    names = cannedKeyAlgoEcc521Names;
                    namesSz = cannedKeyAlgoEcc521NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP256
                case ID_X509V3_ECDSA_SHA2_NISTP256:
                    names = cannedKeyAlgoX509Ecc256Names;
                    namesSz = cannedKeyAlgoX509Ecc256NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP384
                case ID_X509V3_ECDSA_SHA2_NISTP384:
                    names = cannedKeyAlgoX509Ecc384Names;
                    namesSz = cannedKeyAlgoX509Ecc384NamesSz;
                    break;
                #endif
                #ifndef WOLFSSH_NO_ECDSA_SHA2_NISTP521
                case ID_X509V3_ECDSA_SHA2_NISTP521:
                    names = cannedKeyAlgoX509Ecc521Names;
                    namesSz = cannedKeyAlgoX509Ecc521NamesSz;
                    break;
                #endif
                default:
                    WLOG(WS_LOG_DEBUG, "SUAR: ECDSA cert invalid algo");
                    ret = WS_INVALID_ALGO_ID;
            }

            if (ret == WS_SUCCESS) {
                c32toa(rSz + rPad + sSz + sPad + namesSz+ LENGTH_SZ * 4,
                        output + begin);
                begin += LENGTH_SZ;

                c32toa(namesSz, output + begin);
                begin += LENGTH_SZ;

                WMEMCPY(output + begin, names, namesSz);
                begin += namesSz;

                c32toa(rSz + rPad + sSz + sPad + LENGTH_SZ * 2, output + begin);
                begin += LENGTH_SZ;

                c32toa(rSz + rPad, output + begin);
                begin += LENGTH_SZ;

                if (rPad)
                    output[begin++] = 0;

                WMEMCPY(output + begin, r, rSz);
                begin += rSz;

                c32toa(sSz + sPad, output + begin);
                begin += LENGTH_SZ;

                if (sPad)
                    output[begin++] = 0;

                WMEMCPY(output + begin, s, sSz);
                begin += sSz;
            }
        }
    }

    if (ret == WS_SUCCESS)
        *idx = begin;

    if (checkData != NULL) {
        ForceZero(checkData, checkDataSz);
        WFREE(checkData, ssh->ctx->heap, DYNTYPE_TEMP);
    }

    return ret;
}

#endif /* WOLFSSH_CERTS */

#endif /* WOLFSSH_NO_ECDSA */


#if !defined(WOLFSSH_NO_RSA) || !defined(WOLFSSH_NO_ECDSA)
static int PrepareUserAuthRequestPublicKey(WOLFSSH* ssh, word32* payloadSz,
        const WS_UserAuthData* authData, WS_KeySignature* keySig)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering PrepareUserAuthRequestPublicKey()");

    if (ssh == NULL || payloadSz == NULL || authData == NULL || keySig == NULL) {
        ret = WS_BAD_ARGUMENT;
        return ret;
    }

    keySig->keySigId = NameToId(
            (const char*)authData->sf.publicKey.publicKeyType,
            authData->sf.publicKey.publicKeyTypeSz);

    if (ret == WS_SUCCESS) {
        /* Add the boolean size to the payload, and the lengths of
         * the public key algorithm name, and the public key length.
         * For the X509 types, this accounts for ONLY one certificate.*/
        *payloadSz += BOOLEAN_SZ + (LENGTH_SZ * 2) +
            authData->sf.publicKey.publicKeyTypeSz +
            authData->sf.publicKey.publicKeySz;
    }

    switch (keySig->keySigId) {
        #ifndef WOLFSSH_NO_RSA
        case ID_SSH_RSA:
            ret = PrepareUserAuthRequestRsa(ssh,
                    payloadSz, authData, keySig);
            break;
        #ifdef WOLFSSH_CERTS
        case ID_X509V3_SSH_RSA:
            ret = PrepareUserAuthRequestRsaCert(ssh,
                    payloadSz, authData, keySig);
            break;
        #endif
        #endif
        #ifndef WOLFSSH_NO_ECDSA
        case ID_ECDSA_SHA2_NISTP256:
        case ID_ECDSA_SHA2_NISTP384:
        case ID_ECDSA_SHA2_NISTP521:
            ret = PrepareUserAuthRequestEcc(ssh,
                    payloadSz, authData, keySig);
            break;
        #ifdef WOLFSSH_CERTS
        case ID_X509V3_ECDSA_SHA2_NISTP256:
        case ID_X509V3_ECDSA_SHA2_NISTP384:
        case ID_X509V3_ECDSA_SHA2_NISTP521:
            ret = PrepareUserAuthRequestEccCert(ssh,
                    payloadSz, authData, keySig);
            break;
        #endif
        #endif
        default:
            ret = WS_INVALID_ALGO_ID;
    }

    WLOG(WS_LOG_DEBUG, "Leaving PrepareUserAuthRequestPublicKey(), ret = %d",
            ret);
    return ret;
}


static int BuildUserAuthRequestPublicKey(WOLFSSH* ssh,
        byte* output, word32* idx,
        const WS_UserAuthData* authData,
        const byte* sigStart, word32 sigStartIdx,
        WS_KeySignature* keySig)
{
    const WS_UserAuthData_PublicKey* pk;
    word32 begin;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering BuildUserAuthRequestPublicKey()");
    if (ssh == NULL || output == NULL || idx == NULL || authData == NULL ||
            sigStart == NULL || keySig == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        begin = *idx;
        pk = &authData->sf.publicKey;
        output[begin++] = pk->hasSignature;

        if (pk->hasSignature) {
            switch (keySig->keySigId) {
                #ifndef WOLFSSH_NO_RSA
                case ID_SSH_RSA:
                    c32toa(pk->publicKeyTypeSz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin,
                            pk->publicKeyType, pk->publicKeyTypeSz);
                    begin += pk->publicKeyTypeSz;
                    c32toa(pk->publicKeySz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin, pk->publicKey, pk->publicKeySz);
                    begin += pk->publicKeySz;
                    ret = BuildUserAuthRequestRsa(ssh, output, &begin,
                            authData, sigStart, sigStartIdx, keySig);
                    break;
                #ifdef WOLFSSH_CERTS
                case ID_X509V3_SSH_RSA:
                    /* public key type name */
                    c32toa(pk->publicKeyTypeSz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin,
                            pk->publicKeyType, pk->publicKeyTypeSz);
                    begin += pk->publicKeyTypeSz;

                    ret = BuildRFC6187Info(ssh, keySig->keySigId,
                            pk->publicKey, pk->publicKeySz, NULL, 0,
                            output, &ssh->outputBuffer.bufferSz, &begin);
                    if (ret == WS_SUCCESS) {
                        ret = BuildUserAuthRequestRsaCert(ssh, output, &begin,
                            authData, sigStart, sigStartIdx, keySig);
                    }
                    break;
                #endif
                #endif
                #ifndef WOLFSSH_NO_ECDSA
                case ID_ECDSA_SHA2_NISTP256:
                case ID_ECDSA_SHA2_NISTP384:
                case ID_ECDSA_SHA2_NISTP521:
                    c32toa(pk->publicKeyTypeSz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin,
                            pk->publicKeyType, pk->publicKeyTypeSz);
                    begin += pk->publicKeyTypeSz;
                    c32toa(pk->publicKeySz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin, pk->publicKey, pk->publicKeySz);
                    begin += pk->publicKeySz;
                    ret = BuildUserAuthRequestEcc(ssh, output, &begin,
                            authData, sigStart, sigStartIdx, keySig);
                    break;
                #ifdef WOLFSSH_CERTS
                case ID_X509V3_ECDSA_SHA2_NISTP256:
                case ID_X509V3_ECDSA_SHA2_NISTP384:
                case ID_X509V3_ECDSA_SHA2_NISTP521:
                    /* public key type name */
                    c32toa(pk->publicKeyTypeSz, output + begin);
                    begin += LENGTH_SZ;
                    WMEMCPY(output + begin,
                            pk->publicKeyType, pk->publicKeyTypeSz);
                    begin += pk->publicKeyTypeSz;

                    /* build RFC6178 public key to send */
                    ret = BuildRFC6187Info(ssh, keySig->keySigId,
                            pk->publicKey, pk->publicKeySz, NULL, 0,
                            output, &ssh->outputBuffer.bufferSz, &begin);
                    if (ret == WS_SUCCESS) {
                        ret = BuildUserAuthRequestEccCert(ssh, output, &begin,
                            authData, sigStart, sigStartIdx, keySig);
                    }
                    break;
                #endif
                #endif
                default:
                    ret = WS_INVALID_ALGO_ID;
            }
        }
        else {
            /* TODO: Is this right? */
            ret = WS_INVALID_ALGO_ID;
        }

        if (ret == WS_SUCCESS)
            *idx = begin;
    }

    WLOG(WS_LOG_DEBUG, "Leaving BuildUserAuthRequestPublicKey(), ret = %d",
            ret);
    return ret;
}


static void CleanupUserAuthRequestPublicKey(WS_KeySignature* keySig)
{
    if (keySig != NULL) {
        if (keySig->keySigId == ID_SSH_RSA) {
#ifndef WOLFSSH_NO_RSA
            wc_FreeRsaKey(&keySig->ks.rsa.key);
#endif
        }
        else if (keySig->keySigId == ID_ECDSA_SHA2_NISTP256 ||
                keySig->keySigId == ID_ECDSA_SHA2_NISTP384 ||
                keySig->keySigId == ID_ECDSA_SHA2_NISTP521) {
#ifndef WOLFSSH_NO_ECDSA
            wc_ecc_free(&keySig->ks.ecc.key);
#endif
        }
    }
}
#endif


int SendUserAuthRequest(WOLFSSH* ssh, byte authId, int addSig)
{
    byte* output;
    word32 idx;
    const char* authName = NULL;
    word32 authNameSz = 0;
    const char* serviceName = NULL;
    word32 serviceNameSz = 0;
    word32 payloadSz = 0;
    int ret = WS_SUCCESS;
    WS_UserAuthData authData;
    WS_KeySignature *keySig_ptr = NULL;

    WOLFSSH_UNUSED(addSig);

    WLOG(WS_LOG_DEBUG, "Entering SendUserAuthRequest()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        keySig_ptr = (WS_KeySignature*)WMALLOC(sizeof(WS_KeySignature),
                ssh->ctx->heap, DYNTYPE_BUFFER);
        if (!keySig_ptr)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        WMEMSET(keySig_ptr, 0, sizeof(WS_KeySignature));
        keySig_ptr->keySigId = ID_NONE;

        if (ssh->ctx->userAuthCb != NULL) {
            WLOG(WS_LOG_DEBUG, "SUAR: Calling the userauth callback");

            WMEMSET(&authData, 0, sizeof(authData));
            authData.type = authId;
            authData.username = (const byte*)ssh->userName;
            authData.usernameSz = ssh->userNameSz;

            if (authId & WOLFSSH_USERAUTH_PASSWORD) {
                ret = ssh->ctx->userAuthCb(WOLFSSH_USERAUTH_PASSWORD,
                        &authData, ssh->userAuthCtx);
                if (ret != WOLFSSH_USERAUTH_SUCCESS) {
                    WLOG(WS_LOG_DEBUG, "SUAR: Couldn't get password");
                    ret = WS_FATAL_ERROR;
                }
                else {
                    WLOG(WS_LOG_DEBUG, "SUAR: Callback successful password");
                    authData.type = authId = ID_USERAUTH_PASSWORD;
                }
            }
            /* fall into public key case if password case was not successful */
            if ((ret == WS_FATAL_ERROR ||
                !(authId & WOLFSSH_USERAUTH_PASSWORD)) &&
                (authId & WOLFSSH_USERAUTH_PUBLICKEY)) {
                ret = ssh->ctx->userAuthCb(WOLFSSH_USERAUTH_PUBLICKEY,
                        &authData, ssh->userAuthCtx);
                if (ret != WOLFSSH_USERAUTH_SUCCESS) {
                    WLOG(WS_LOG_DEBUG, "SUAR: Couldn't get key");
                    ret = WS_FATAL_ERROR;
                }
                else {
                    WLOG(WS_LOG_DEBUG, "SUAR: Callback successful public key");
                    authData.type = authId = ID_USERAUTH_PUBLICKEY;
                }
            }

        }
        else {
            WLOG(WS_LOG_DEBUG, "SUAR: No user auth callback");
            ret = WS_FATAL_ERROR;
        }
    }

    if (ret == WS_SUCCESS) {
        serviceName = IdToName(ID_SERVICE_CONNECTION);
        serviceNameSz = (word32)WSTRLEN(serviceName);
        authName = IdToName(authId);
        authNameSz = (word32)WSTRLEN(authName);

        payloadSz = MSG_ID_SZ + (LENGTH_SZ * 3) +
                    ssh->userNameSz + serviceNameSz + authNameSz;

        if (authId == ID_USERAUTH_PASSWORD)
            ret = PrepareUserAuthRequestPassword(ssh, &payloadSz, &authData);
        else if (authId == ID_USERAUTH_PUBLICKEY && !ssh->userAuthPkDone) {
            authData.sf.publicKey.hasSignature = 1;
            ssh->userAuthPkDone = 1;
            ret = PrepareUserAuthRequestPublicKey(ssh, &payloadSz, &authData,
                    keySig_ptr);
        }
        else if (authId != ID_NONE && !ssh->userAuthPkDone)
            ret = WS_INVALID_ALGO_ID;
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, payloadSz);

    if (ret == WS_SUCCESS) {
        byte* sigStart;
        word32 sigStartIdx;

        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        sigStart = output + idx;
        sigStartIdx = idx;

        output[idx++] = MSGID_USERAUTH_REQUEST;
        c32toa(ssh->userNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, ssh->userName, ssh->userNameSz);
        idx += ssh->userNameSz;

        c32toa(serviceNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, serviceName, serviceNameSz);
        idx += serviceNameSz;

        c32toa(authNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, authName, authNameSz);
        idx += authNameSz;

        if (authId == ID_USERAUTH_PASSWORD) {
            WOLFSSH_UNUSED(sigStart);
            WOLFSSH_UNUSED(sigStartIdx);

            ret = BuildUserAuthRequestPassword(ssh, output, &idx, &authData);
        }
        else if (authId == ID_USERAUTH_PUBLICKEY)
            ret = BuildUserAuthRequestPublicKey(ssh, output, &idx, &authData,
                    sigStart, sigStartIdx, keySig_ptr);

        if (ret == WS_SUCCESS) {
            ssh->outputBuffer.length = idx;
            ret = BundlePacket(ssh);
        }
    }

    if (authId == ID_USERAUTH_PUBLICKEY)
        CleanupUserAuthRequestPublicKey(keySig_ptr);

    if (ret == WS_SUCCESS) {
        ret = wolfSSH_SendPacket(ssh);
    }

    if (ret != WS_WANT_WRITE && ret != WS_SUCCESS)
        PurgePacket(ssh);

    ForceZero(&authData, sizeof(WS_UserAuthData));
    WLOG(WS_LOG_DEBUG, "Leaving SendUserAuthRequest(), ret = %d", ret);

    if (keySig_ptr)
        WFREE(keySig_ptr, ssh->ctx->heap, DYNTYPE_BUFFER);

    return ret;
}

#ifndef MAX_AUTH_STRING
    #define MAX_AUTH_STRING 80
#endif
static int GetAllowedAuth(WOLFSSH* ssh, char* authStr)
{
    int typeAllowed = 0;

    typeAllowed |= WOLFSSH_USERAUTH_PASSWORD;
#if !defined(WOLFSSH_NO_RSA) || !defined(WOLFSSH_NO_ECDSA)
    typeAllowed |= WOLFSSH_USERAUTH_PUBLICKEY;
#endif

    if (ssh == NULL || authStr == NULL)
        return WS_BAD_ARGUMENT;

    authStr[0] = '\0';
    if (ssh->ctx && ssh->ctx->userAuthTypesCb) {
        typeAllowed = ssh->ctx->userAuthTypesCb(ssh, ssh->userAuthCtx);
    }
    if (typeAllowed & WOLFSSH_USERAUTH_PUBLICKEY) {
        WSTRNCAT(authStr, "publickey,", MAX_AUTH_STRING-1);
    }

    if (typeAllowed & WOLFSSH_USERAUTH_PASSWORD) {
        WSTRNCAT(authStr, "password,", MAX_AUTH_STRING-1);
    }

    /* remove last comma from the list */
    return (int)XSTRLEN(authStr) - 1;
}

int SendUserAuthFailure(WOLFSSH* ssh, byte partialSuccess)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    int   authSz;
    char  authStr[MAX_AUTH_STRING];

    WLOG(WS_LOG_DEBUG, "Entering SendUserAuthFailure()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        authSz = GetAllowedAuth(ssh, authStr);
        if (authSz < 0) {
            ret = authSz; /* propogate error value */
        }
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh,
                            MSG_ID_SZ + LENGTH_SZ +
                            authSz + BOOLEAN_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_USERAUTH_FAILURE;
        c32toa(authSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, authStr, authSz);
        idx += authSz;
        output[idx++] = (partialSuccess != 0);

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}


int SendUserAuthSuccess(WOLFSSH* ssh)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_USERAUTH_SUCCESS;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}


int SendUserAuthPkOk(WOLFSSH* ssh,
                     const byte* algoName, word32 algoNameSz,
                     const byte* publicKey, word32 publicKeySz)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;

    if (ssh == NULL ||
        algoName == NULL || algoNameSz == 0 ||
        publicKey == NULL || publicKeySz == 0) {

        ret = WS_BAD_ARGUMENT;
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + (LENGTH_SZ * 2) +
                                 algoNameSz + publicKeySz);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_USERAUTH_PK_OK;
        c32toa(algoNameSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, algoName, algoNameSz);
        idx += algoNameSz;
        c32toa(publicKeySz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, publicKey, publicKeySz);
        idx += publicKeySz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    return ret;
}


int SendUserAuthBanner(WOLFSSH* ssh)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    const char* banner = NULL;
    word32 bannerSz = 0;

    WLOG(WS_LOG_DEBUG, "Entering SendUserAuthBanner()");
    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        banner = ssh->ctx->banner;
        bannerSz = ssh->ctx->bannerSz;
    }

    if (banner != NULL && bannerSz > 0) {
        if (ret == WS_SUCCESS)
            ret = PreparePacket(ssh, MSG_ID_SZ + (LENGTH_SZ * 2) +
                                bannerSz + cannedLangTagSz);

        if (ret == WS_SUCCESS) {
            output = ssh->outputBuffer.buffer;
            idx = ssh->outputBuffer.length;

            output[idx++] = MSGID_USERAUTH_BANNER;
            c32toa(bannerSz, output + idx);
            idx += LENGTH_SZ;
            if (bannerSz > 0)
                WMEMCPY(output + idx, banner, bannerSz);
            idx += bannerSz;
            c32toa(cannedLangTagSz, output + idx);
            idx += LENGTH_SZ;
            WMEMCPY(output + idx, cannedLangTag, cannedLangTagSz);
            idx += cannedLangTagSz;

            ssh->outputBuffer.length = idx;

            ret = BundlePacket(ssh);
        }
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendUserAuthBanner()");
    return ret;
}


int SendRequestSuccess(WOLFSSH *ssh, int success)
{
    byte *output;
    word32 idx;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendRequestSuccess(), %s",
         success ? "Success" : "Failure");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ);

    if (ret == WS_SUCCESS)
    {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = success ? MSGID_REQUEST_SUCCESS : MSGID_REQUEST_FAILURE;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendRequestSuccess(), ret = %d", ret);
    return ret;
}


int SendGlobalRequestFwdSuccess(WOLFSSH* ssh, int success, word32 port)
{
    byte *output;
    word32 idx;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendGlobalRequestFwdSuccess(), %s",
         success ? "Success" : "Failure");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + (success ? UINT32_SZ : 0));

    if (ret == WS_SUCCESS)
    {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        if (success) {
            output[idx++] = MSGID_REQUEST_SUCCESS;
            c32toa(port, output + idx);
            idx += UINT32_SZ;
        }
        else {
            output[idx++] = MSGID_REQUEST_FAILURE;
        }

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendGlobalRequestFwdSuccess(), ret = %d", ret);
    return ret;
}


static int SendChannelOpen(WOLFSSH* ssh, WOLFSSH_CHANNEL* channel,
        byte* channelData, word32 channelDataSz)
{
    byte* output;
    const char* channelType = NULL;
    word32 channelTypeSz = 0, idx;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelOpen()");

    if (ssh == NULL || channel == NULL)
        ret = WS_BAD_ARGUMENT;
    if (channelDataSz > 0 && channelData == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channelType = IdToName(channel->channelType);
        channelTypeSz = (word32)WSTRLEN(channelType);

        ret = PreparePacket(ssh, MSG_ID_SZ + LENGTH_SZ + channelTypeSz +
                                 (UINT32_SZ * 3) + channelDataSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_OPEN;
        c32toa(channelTypeSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, channelType, channelTypeSz);
        idx += channelTypeSz;
        c32toa(channel->channel, output + idx);
        idx += UINT32_SZ;
        c32toa(channel->windowSz, output + idx);
        idx += UINT32_SZ;
        c32toa(channel->maxPacketSz, output + idx);
        idx += UINT32_SZ;
        if (channelDataSz > 0)
            WMEMCPY(output + idx, channelData, channelDataSz);
        idx += channelDataSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelOpen(), ret = %d", ret);
    return ret;
}


int SendChannelOpenSession(WOLFSSH* ssh, WOLFSSH_CHANNEL* channel)
{
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelOpenSession()");

    ret = SendChannelOpen(ssh, channel, NULL, 0);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelOpenSession(), ret = %d", ret);
    return ret;
}


#ifdef WOLFSSH_FWD
int SendChannelOpenForward(WOLFSSH* ssh, WOLFSSH_CHANNEL* channel)
{
    int ret = WS_SUCCESS;
    byte* forwardData = NULL;
    word32 hostSz, originSz, forwardDataSz, idx;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelOpenForward()");

    if (ssh == NULL || channel == NULL ||
            channel->host == NULL || channel->origin == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        hostSz = (word32)WSTRLEN(channel->host);
        originSz = (word32)WSTRLEN(channel->origin);
        forwardDataSz = UINT32_SZ * 2 + LENGTH_SZ * 2 + hostSz + originSz;
        forwardData = (byte*)WMALLOC(forwardDataSz,
                ssh->ctx->heap, DYNTYPE_TEMP);
        if (forwardData == NULL)
            ret = WS_MEMORY_E;
    }

    if (ret == WS_SUCCESS) {
        c32toa(hostSz, forwardData);
        idx = LENGTH_SZ;
        WMEMCPY(forwardData + idx, channel->host, hostSz);
        idx += hostSz;
        c32toa(channel->hostPort, forwardData + idx);
        idx += UINT32_SZ;
        c32toa(originSz, forwardData + idx);
        idx += LENGTH_SZ;
        WMEMCPY(forwardData + idx, channel->origin, originSz);
        idx += originSz;
        c32toa(channel->originPort, forwardData + idx);

        ret = SendChannelOpen(ssh, channel, forwardData, forwardDataSz);
    }

    if (forwardData)
        WFREE(forwardData, ssh->ctx->heap, DYNTYPE_TEMP);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelOpenForward(), ret = %d", ret);
    return ret;
}
#endif /* WOLFSSH_FWD */


int SendChannelOpenConf(WOLFSSH* ssh, WOLFSSH_CHANNEL* channel)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelOpenConf()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "  channelId = %u", channel->channel);
        WLOG(WS_LOG_INFO, "  peerChannelId = %u", channel->peerChannel);
        WLOG(WS_LOG_INFO, "  peerWindowSz = %u", channel->peerWindowSz);
        WLOG(WS_LOG_INFO, "  peerMaxPacketSz = %u", channel->peerMaxPacketSz);
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + (UINT32_SZ * 4));

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_OPEN_CONF;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(channel->channel, output + idx);
        idx += UINT32_SZ;
        c32toa(channel->windowSz, output + idx);
        idx += UINT32_SZ;
        c32toa(channel->maxPacketSz, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelOpenConf(), ret = %d", ret);
    return ret;
}


int SendChannelEof(WOLFSSH* ssh, word32 peerChannelId)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelEof()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, peerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        if (channel->eofTxd) {
            WLOG(WS_LOG_DEBUG, "Already sent EOF");
            WLOG(WS_LOG_DEBUG, "Leaving SendChannelEof(), ret = %d", ret);
            return ret;
        }
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_EOF;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    if (ret == WS_SUCCESS)
        channel->eofTxd = 1;

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelEof(), ret = %d", ret);
    return ret;
}


int SendChannelEow(WOLFSSH* ssh, word32 peerChannelId)
{
    byte* output;
    word32 idx;
    word32 strSz = sizeof("eow@openssh.com");
    int      ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelEow()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS && !ssh->clientOpenSSH) {
        WLOG(WS_LOG_DEBUG, "Leaving SendChannelEow(), not OpenSSH");
        return ret;
    }

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, peerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + LENGTH_SZ + strSz +
                            BOOLEAN_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_REQUEST;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(strSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, "eow@openssh.com", strSz);
        idx += strSz;
        output[idx++] = 0;      // false

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelEow(), ret = %d", ret);
    return ret;
}


int SendChannelExit(WOLFSSH* ssh, word32 peerChannelId, int status)
{
    byte* output;
    word32 idx;
    word32 strSz = sizeof("exit-status");
    int      ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelExit(), status = %d", status);

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, peerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + LENGTH_SZ + strSz +
                            BOOLEAN_SZ + UINT32_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_REQUEST;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(strSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, "exit-status", strSz);
        idx += strSz;
        output[idx++] = 0;      // false
        c32toa(status, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelExit(), ret = %d", ret);
    return ret;
}


int SendChannelClose(WOLFSSH* ssh, word32 peerChannelId)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelClose()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, peerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
        else if (channel->closeTxd) {
            WLOG(WS_LOG_DEBUG, "Leaving SendChannelClose(), already sent");
            return ret;
        }
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_CLOSE;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS) {
        ret = wolfSSH_SendPacket(ssh);
        channel->closeTxd = 1;
    }

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelClose(), ret = %d", ret);
    return ret;
}


int SendChannelData(WOLFSSH* ssh, word32 channelId,
                    byte* data, word32 dataSz)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelData()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        if (ssh->isKeying)
            ret = WS_REKEYING;
    }

    /* if already having data pending try to flush it first and do not continue
     * to que more on fail */
    if (ret == WS_SUCCESS && ssh->outputBuffer.plainSz > 0) {
        WLOG(WS_LOG_DEBUG, "Flushing out want write data");
        ret = wolfSSH_SendPacket(ssh);
        if (ret != WS_SUCCESS) {
            WLOG(WS_LOG_DEBUG, "Leaving SendChannelData(), ret = %d", ret);
            return ret;
        }

    }

    if (ret == WS_SUCCESS) {
        if (ssh->outputBuffer.length != 0)
            ret = wolfSSH_SendPacket(ssh);
    }

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL) {
            WLOG(WS_LOG_DEBUG, "Invalid channel");
            ret = WS_INVALID_CHANID;
        }
    }

    if (ret == WS_SUCCESS) {
        if (channel->peerWindowSz == 0) {
            WLOG(WS_LOG_DEBUG, "channel window is full");
            ret = WS_WINDOW_FULL;
        }
    }

    if (ret == WS_SUCCESS) {
        word32 bound = min(channel->peerWindowSz, channel->peerMaxPacketSz);

        if (dataSz > bound) {
            WLOG(WS_LOG_DEBUG,
                 "Trying to send %u, client will only accept %u, limiting",
                 dataSz, bound);
            dataSz = bound;
        }

        ret = PreparePacket(ssh,
                MSG_ID_SZ + UINT32_SZ + LENGTH_SZ + dataSz);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_DATA;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(dataSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, data, dataSz);
        idx += dataSz;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS) {
        WLOG(WS_LOG_INFO, "  dataSz = %u", dataSz);
        WLOG(WS_LOG_INFO, "  peerWindowSz = %u", channel->peerWindowSz);
        channel->peerWindowSz -= dataSz;
        WLOG(WS_LOG_INFO, "  update peerWindowSz = %u", channel->peerWindowSz);
    }

    /* at this point the data has been loaded into WOLFSSH structure and is
     * considered consumed */
    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    if (ret == WS_SUCCESS || ret == WS_WANT_WRITE)
        ret = dataSz;

    if (ssh && ssh->error == WS_WANT_WRITE)
        ssh->outputBuffer.plainSz = dataSz;

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelData(), ret = %d", ret);
    return ret;
}


int SendChannelWindowAdjust(WOLFSSH* ssh, word32 channelId,
                            word32 bytesToAdd)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelWindowAdjust()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
    if (channel == NULL) {
        WLOG(WS_LOG_DEBUG, "Invalid channel");
        ret = WS_INVALID_CHANID;
    }
    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + (UINT32_SZ * 2));

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_WINDOW_ADJUST;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(bytesToAdd, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelWindowAdjust(), ret = %d", ret);
    return ret;
}


static const char cannedShellName[] = "shell";
static const word32 cannedShellNameSz = sizeof(cannedShellName) - 1;

static const char cannedSubName[] = "subsystem";
static const word32 cannedSubNameSz = sizeof(cannedSubName) - 1;

static const char cannedExecName[] = "exec";
static const word32 cannedExecNameSz = sizeof(cannedExecName) - 1;


/* name : command for exec and name for subsystem channels */
int SendChannelRequest(WOLFSSH* ssh, byte* name, word32 nameSz)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;
    const char* cType = NULL;
    word32 typeSz = 0;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelRequest()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh,
                ssh->defaultPeerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        switch (ssh->connectChannelId) {
            case WOLFSSH_SESSION_SHELL:
                cType  = cannedShellName;
                typeSz = cannedShellNameSz;
                break;

            case WOLFSSH_SESSION_EXEC:
                cType  = cannedExecName;
                typeSz = cannedExecNameSz;
                break;

            case WOLFSSH_SESSION_SUBSYSTEM:
                cType  = cannedSubName;
                typeSz = cannedSubNameSz;
                break;

            default:
                WLOG(WS_LOG_DEBUG, "Unknown channel type");
                return WS_BAD_ARGUMENT;
        }
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + LENGTH_SZ +
                                 typeSz + BOOLEAN_SZ +
                                 ((nameSz > 0)? UINT32_SZ : 0) + nameSz);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_REQUEST;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(typeSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, cType, typeSz);
        idx += typeSz;
        output[idx++] = 1;

        if (nameSz > 0) {
            c32toa(nameSz, output + idx);
            idx += UINT32_SZ;
            WMEMCPY(output + idx, name, nameSz);
            idx += nameSz;
        }

        ssh->outputBuffer.length = idx;

        WLOG(WS_LOG_INFO, "Sending Channel Request: ");
        WLOG(WS_LOG_INFO, "  channelId = %u", channel->peerChannel);
        WLOG(WS_LOG_INFO, "  type = %s", cType);
        WLOG(WS_LOG_INFO, "  wantReply = %u", 1);

    #ifdef DEBUG_WOLFSSH
        /* only compile in code for checks on type if in debug mode */
        switch (ssh->connectChannelId) {
            case WOLFSSH_SESSION_EXEC:
                WLOG(WS_LOG_INFO, "  command = %s", name);
                break;

            case WOLFSSH_SESSION_SUBSYSTEM:
                WLOG(WS_LOG_INFO, "  subsystem = %s", name);
                break;

            default:
                break;
        }
    #endif

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelRequest(), ret = %d", ret);
    return ret;
}


#if defined(WOLFSSH_TERM) && !defined(NO_FILESYSTEM)

#if !defined(USE_WINDOWS_API) && !defined(MICROCHIP_PIC32) && \
    !defined(NO_TERMIOS)

    #include <unistd.h>

    /* sets terminal mode in buffer and advances idx */
    static void TTYSet(word32 isSet, int type, byte* out, word32* idx)
    {
        if (isSet) isSet = 1;
        out[*idx] = type; *idx += 1;
        c32toa(isSet, out + *idx); *idx += UINT32_SZ;
    }

    static void TTYCharSet(char flag, int type, byte* out, word32* idx)
    {
        out[*idx] = type; *idx += 1;
        c32toa(flag, out + *idx); *idx += UINT32_SZ;
    }
#endif /* !USE_WINDOWS_API && !MICROCHIP_PIC32 && !NO_TERMIOS*/


/* create terminal mode string for pseudo-terminal request
 * returns size of buffer */
static int CreateMode(WOLFSSH* ssh, byte* mode)
{
    word32 idx = 0;
    int baud = 38400; /* default speed */

    #if !defined(USE_WINDOWS_API) && !defined(MICROCHIP_PIC32) && \
        !defined(NO_TERMIOS)
    {
        WOLFSSH_TERMIOS term;

        if (tcgetattr(STDIN_FILENO, &term) != 0) {
            printf("Couldn't get the original terminal settings.\n");
            return -1;
        }

        /* set baud rate */
        baud = (int)cfgetospeed(&term);

        /* char type */
        TTYCharSet(term.c_cc[VINTR], WOLFSSH_VINTR, mode, &idx);
        TTYCharSet(term.c_cc[VQUIT], WOLFSSH_VQUIT, mode, &idx);
        TTYCharSet(term.c_cc[VERASE], WOLFSSH_VERASE, mode, &idx);
        TTYCharSet(term.c_cc[VKILL], WOLFSSH_VKILL, mode, &idx);
        TTYCharSet(term.c_cc[VEOF], WOLFSSH_VEOF, mode, &idx);
        TTYCharSet(term.c_cc[VEOL], WOLFSSH_VEOL, mode, &idx);
        TTYCharSet(term.c_cc[VEOL2], WOLFSSH_VEOL2, mode, &idx);
        TTYCharSet(term.c_cc[VSTART], WOLFSSH_VSTART, mode, &idx);
        TTYCharSet(term.c_cc[VSTOP], WOLFSSH_VSTOP, mode, &idx);
        TTYCharSet(term.c_cc[VSUSP], WOLFSSH_VSUSP, mode, &idx);
        #ifdef VDSUSP
            TTYCharSet(term.c_cc[VDSUSP], WOLFSSH_VDSUSP, mode, &idx);
        #endif
        TTYCharSet(term.c_cc[VREPRINT], WOLFSSH_VREPRINT, mode, &idx);
        TTYCharSet(term.c_cc[VWERASE], WOLFSSH_VWERASE, mode, &idx);
        TTYCharSet(term.c_cc[VLNEXT], WOLFSSH_VLNEXT, mode, &idx);
        #ifdef VFLUSH
            TTYCharSet(term.c_cc[VFLUSH], WOLFSSH_VFLUSH, mode, &idx);
        #endif
        #ifdef VSWTCH
            TTYCharSet(term.c_cc[VSWTCH], WOLFSSH_VSWTCH, mode, &idx);
        #endif
        #ifdef VSTATUS
            TTYCharSet(term.c_cc[VSTATUS], WOLFSSH_VSTATUS, mode, &idx);
        #endif
        TTYCharSet(term.c_cc[VDISCARD], WOLFSSH_VDISCARD, mode, &idx);

        /* c_iflag for input modes */
        TTYSet((term.c_iflag & IGNPAR), WOLFSSH_IGNPAR, mode, &idx);
        TTYSet((term.c_iflag & PARMRK), WOLFSSH_PARMRK, mode, &idx);
        TTYSet((term.c_iflag & INPCK), WOLFSSH_INPCK, mode, &idx);
        TTYSet((term.c_iflag & ISTRIP), WOLFSSH_ISTRIP, mode, &idx);
        TTYSet((term.c_iflag & INLCR), WOLFSSH_INLCR, mode, &idx);
        TTYSet((term.c_iflag & IGNCR), WOLFSSH_IGNCR, mode, &idx);
        TTYSet((term.c_iflag & ICRNL), WOLFSSH_ICRNL, mode, &idx);
        #ifdef IUCLC
            TTYSet((term.c_iflag & IUCLC), WOLFSSH_IUCLC, mode, &idx);
        #endif
        TTYSet((term.c_iflag & IXON), WOLFSSH_IXON, mode, &idx);
        TTYSet((term.c_iflag & IXANY), WOLFSSH_IXANY, mode, &idx);
        TTYSet((term.c_iflag & IXOFF), WOLFSSH_IXOFF, mode, &idx);
        TTYSet((term.c_iflag & IMAXBEL), WOLFSSH_IMAXBEL, mode, &idx);

        /* c_lflag */
        TTYSet((term.c_lflag & ISIG), WOLFSSH_ISIG, mode, &idx);
        TTYSet((term.c_lflag &  ICANON), WOLFSSH_ICANON, mode, &idx);
        #ifdef XCASE
            TTYSet((term.c_lflag &  XCASE), WOLFSSH_XCASE, mode, &idx);
        #endif
        TTYSet((term.c_lflag &  ECHO), WOLFSSH_ECHO, mode, &idx);
        TTYSet((term.c_lflag &  ECHOE), WOLFSSH_ECHOE, mode, &idx);
        TTYSet((term.c_lflag &  ECHOK), WOLFSSH_ECHOK, mode, &idx);
        TTYSet((term.c_lflag &  ECHONL), WOLFSSH_ECHONL, mode, &idx);
        TTYSet((term.c_lflag &  NOFLSH), WOLFSSH_NOFLSH, mode, &idx);
        TTYSet((term.c_lflag &  TOSTOP), WOLFSSH_TOSTOP, mode, &idx);
        TTYSet((term.c_lflag &  IEXTEN), WOLFSSH_IEXTEN, mode, &idx);
        TTYSet((term.c_lflag &  ECHOCTL), WOLFSSH_ECHOCTL, mode, &idx);
        TTYSet((term.c_lflag &  ECHOKE), WOLFSSH_ECHOKE, mode, &idx);
        #ifdef PENDIN
            TTYSet((term.c_lflag &  PENDIN), WOLFSSH_PENDIN, mode, &idx);
        #endif

        /* c_oflag */
        TTYSet((term.c_oflag &  OPOST), WOLFSSH_OPOST, mode, &idx);
        #ifdef OLCUC
            TTYSet((term.c_oflag &  OLCUC), WOLFSSH_OLCUC, mode, &idx);
        #endif
        TTYSet((term.c_oflag &  ONLCR), WOLFSSH_ONLCR, mode, &idx);
        TTYSet((term.c_oflag &  OCRNL), WOLFSSH_OCRNL, mode, &idx);
        TTYSet((term.c_oflag &  ONOCR), WOLFSSH_ONOCR, mode, &idx);
        TTYSet((term.c_oflag &  ONLRET), WOLFSSH_ONLRET, mode, &idx);

        /* c_cflag */
        TTYSet((term.c_cflag &  CS7), WOLFSSH_CS7, mode, &idx);
        TTYSet((term.c_cflag &  CS8), WOLFSSH_CS8, mode, &idx);
        TTYSet((term.c_cflag &  PARENB), WOLFSSH_PARENB, mode, &idx);
        TTYSet((term.c_cflag &  PARODD), WOLFSSH_PARODD, mode, &idx);
    }
    #endif /* !USE_WINDOWS_API && !MICROCHIP_PIC32 && !NO_TERMIOS */

    mode[idx++] = WOLFSSH_TTY_OP_OSPEED;
    c32toa(baud, mode + idx); idx += UINT32_SZ;
    mode[idx++] = WOLFSSH_TTY_OP_ISPEED;
    c32toa(baud, mode + idx); idx += UINT32_SZ;

    WOLFSSH_UNUSED(ssh);
    mode[idx++] = WOLFSSH_TTY_OP_END;
    return idx;
}


/* sends request for pseudo-terminal (rfc 4254)
 * returns WS_SUCCESS on success */
int SendChannelTerminalRequest(WOLFSSH* ssh)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel;
    const char cType[] = "pty-req";
    const char envVar[] = "xterm";
    byte mode[4096];
    word32 envSz, typeSz, modeSz;
    word32 w = 80, h = 24;
    word32 pxW = 0, pxH = 0;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelTerminalRequest()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    envSz  = (word32)WSTRLEN(envVar);
    typeSz = (word32)WSTRLEN(cType);
    modeSz = CreateMode(ssh, mode);

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh,
                ssh->defaultPeerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    /*     craft packet with the following structure
     *     byte      MSGID_CHANNEL_REQUEST
     *     word32    channel
     *     string    "pty-req"
     *     boolean   want_reply
     *     string    term environment variable
     *     word32    terminal width
     *     word32    terminal height
     *     word32    terminal width (pixels)
     *     word32    terminal height (pixels)
     *     string    encoded terminal modes
     */

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + LENGTH_SZ +
                                 typeSz + BOOLEAN_SZ +
                                 ((envSz > 0)? UINT32_SZ : 0) + envSz +
                                 UINT32_SZ * 4 +
                                 ((modeSz > 0)? UINT32_SZ : 0) + modeSz);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx    = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_REQUEST;
        c32toa(channel->peerChannel, output + idx); idx += UINT32_SZ;
        c32toa(typeSz, output + idx);               idx += LENGTH_SZ;
        WMEMCPY(output + idx, cType, typeSz);       idx += typeSz;
        output[idx++] = 1; /* want reply */

        if (envSz > 0) {
            c32toa(envSz, output + idx);          idx += UINT32_SZ;
            WMEMCPY(output + idx, envVar, envSz); idx += envSz;
        }

        c32toa(w, output + idx);   idx += UINT32_SZ;
        c32toa(h, output + idx);   idx += UINT32_SZ;
        c32toa(pxW, output + idx); idx += UINT32_SZ;
        c32toa(pxH, output + idx); idx += UINT32_SZ;

        if (modeSz > 0) {
            c32toa(modeSz, output + idx);          idx += UINT32_SZ;
            WMEMCPY(output + idx, mode, modeSz);   idx += modeSz;
        }

        ssh->outputBuffer.length = idx;

        WLOG(WS_LOG_INFO, "Sending Pseudo-Terminal Channel Request: ");
        WLOG(WS_LOG_INFO, "  channelId = %u", channel->peerChannel);
        WLOG(WS_LOG_INFO, "  type = %s", cType);
        WLOG(WS_LOG_INFO, "  wantReply = %u", 1);
        WLOG(WS_LOG_INFO, "  (width , height) = (%d , %d)", w, h);
        WLOG(WS_LOG_INFO, "  pixels (width , height) = (%d , %d)", pxW, pxH);
        WLOG(WS_LOG_INFO, "  term mode = %s", mode);


        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelTerminalRequest(), ret = %d", ret);
    return ret;
}
#endif /* WOLFSSH_TERM */


#ifdef WOLFSSH_AGENT

int SendChannelAgentRequest(WOLFSSH* ssh)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel;
    const char* cType = "auth-agent-req@openssh.com";
    word32 typeSz;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelRequestAgent()");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh,
                ssh->defaultPeerChannelId, WS_CHANNEL_ID_PEER);
        if (channel == NULL)
            ret = WS_INVALID_CHANID;
    }

    if (ret == WS_SUCCESS) {
        typeSz = (word32)WSTRLEN(cType);
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ + LENGTH_SZ +
                                 typeSz + BOOLEAN_SZ);
    }

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = MSGID_CHANNEL_REQUEST;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;
        c32toa(typeSz, output + idx);
        idx += LENGTH_SZ;
        WMEMCPY(output + idx, cType, typeSz);
        idx += typeSz;
        output[idx++] = 0;

        ssh->outputBuffer.length = idx;

        WLOG(WS_LOG_INFO, "Sending Channel Request: ");
        WLOG(WS_LOG_INFO, "  channelId = %u", channel->peerChannel);
        WLOG(WS_LOG_INFO, "  type = %s", cType);
        WLOG(WS_LOG_INFO, "  wantReply = %u", 0);

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelRequestAgent(), ret = %d", ret);
    return ret;
}

#endif /* WOLFSSH_AGENT */


int SendChannelSuccess(WOLFSSH* ssh, word32 channelId, int success)
{
    byte* output;
    word32 idx;
    int ret = WS_SUCCESS;
    WOLFSSH_CHANNEL* channel = NULL;

    WLOG(WS_LOG_DEBUG, "Entering SendChannelSuccess(), %s",
         success ? "Success" : "Failure");

    if (ssh == NULL)
        ret = WS_BAD_ARGUMENT;

    if (ret == WS_SUCCESS) {
        channel = ChannelFind(ssh, channelId, WS_CHANNEL_ID_SELF);
        if (channel == NULL) {
            WLOG(WS_LOG_DEBUG, "Invalid channel");
            ret = WS_INVALID_CHANID;
        }
    }

    if (ret == WS_SUCCESS)
        ret = PreparePacket(ssh, MSG_ID_SZ + UINT32_SZ);

    if (ret == WS_SUCCESS) {
        output = ssh->outputBuffer.buffer;
        idx = ssh->outputBuffer.length;

        output[idx++] = success ?
                        MSGID_CHANNEL_SUCCESS : MSGID_CHANNEL_FAILURE;
        c32toa(channel->peerChannel, output + idx);
        idx += UINT32_SZ;

        ssh->outputBuffer.length = idx;

        ret = BundlePacket(ssh);
    }

    if (ret == WS_SUCCESS)
        ret = wolfSSH_SendPacket(ssh);

    WLOG(WS_LOG_DEBUG, "Leaving SendChannelSuccess(), ret = %d", ret);
    return ret;
}


#if (defined(WOLFSSH_SFTP) || defined(WOLFSSH_SCP)) && \
    !defined(NO_WOLFSSH_SERVER)
/* cleans up absolute path
 * returns size of new path on success (strlen sz) and negative values on fail*/
int wolfSSH_CleanPath(WOLFSSH* ssh, char* in)
{
    int  i;
    long sz;
    byte found;
    char *path;
    void *heap = NULL;

    if (in == NULL) {
        return WS_BAD_ARGUMENT;
    }

    if (ssh != NULL) {
        heap = ssh->ctx->heap;
    }

    sz   = (long)WSTRLEN(in);
    path = (char*)WMALLOC(sz+1, heap, DYNTYPE_PATH);
    if (path == NULL) {
        return WS_MEMORY_E;
    }
    WMEMCPY(path, in, sz);
    path[sz] = '\0';

#if defined(WOLFSSL_NUCLEUS) || defined(USE_WINDOWS_API)
    for (i = 0; i < sz; i++) {
        if (path[i] == '/') path[i] = '\\';
    }
#endif
    sz = (long)WSTRLEN(path);

    /* remove any /./ patterns, direcotries, exclude cases like ./ok./test */
    for (i = 1; i + 1 < sz; i++) {
        if (path[i] == '.' && path[i - 1] == WS_DELIM && path[i + 1] == WS_DELIM) {
            WMEMMOVE(path + (i-1), path + (i+1), sz - (i-1));
            sz -= 2; /* removed '/.' from string*/
            i--;
        }
    }

    /* remove any double '/' or '\' chars */
    for (i = 0; i < sz; i++) {
        if ((path[i] == WS_DELIM && path[i+1] == WS_DELIM)) {
            WMEMMOVE(path + i, path + i + 1, sz - i);
            sz -= 1;
            i--;
        }
    }

    if (path != NULL) {
        /* go through path until no cases are found */
        do {
            int prIdx = 0; /* begin of cut */
            int enIdx = 0; /* end of cut */
            sz = (long)WSTRLEN(path);

            found = 0;
            for (i = 1; i < sz; i++) {
                if (path[i] == WS_DELIM) {
                    int z;

                    /* if next two chars are .. then delete */
                    if (path[i+1] == '.' && path[i+2] == '.') {
                        enIdx = i + 3;

                        /* start at one char before / and retrace path */
                        for (z = i - 1; z > 0; z--) {
                            if (path[z] == WS_DELIM || path[z] == ':') {
                                prIdx = z;
                                break;
                            }
                        }

                        /* cut out .. and previous */
                        WMEMMOVE(path + prIdx, path + enIdx, sz - enIdx);
                        path[sz - (enIdx - prIdx)] = '\0';

                        if (enIdx == sz) {
                            path[prIdx] = '\0';
                        }

                        /* case of at / */
                        if (WSTRLEN(path) == 0) {
                           path[0] = '/';
                           path[1] = '\0';
                        }

                        found = 1;
                        break;
                    }
                }
            }
        } while (found);

#if defined(WOLFSSL_NUCLEUS) || defined(USE_WINDOWS_API)
        sz = (long)WSTRLEN(path);

        if (path[sz - 1] == ':') {
            path[sz] = WS_DELIM;
            path[sz + 1] = '\0';
        }

        /* clean up any multiple drive listed i.e. A:/A: */
        {
            int i,j;
            sz = (long)WSTRLEN(path);
            for (i = 0, j = 0; i < sz; i++) {
                if (path[i] == ':') {
                    if (j == 0) j = i;
                    else {
                        /* @TODO only checking once */
                        WMEMMOVE(path, path + i - WS_DRIVE_SIZE,
                                sz - i + WS_DRIVE_SIZE);
                        path[sz - i + WS_DRIVE_SIZE] = '\0';
                        break;
                    }
                }
            }
        }

        /* remove leading '/' for nucleus. Preserve case of single "/" */
        sz = (long)WSTRLEN(path);
        while (sz > 2 && path[0] == WS_DELIM) {
            sz--;
            WMEMMOVE(path, path + 1, sz);
            path[sz] = '\0';
        }
#endif

#ifndef FREESCALE_MQX
        /* remove trailing delimiter */
        if (sz > 3 && path[sz - 1] == WS_DELIM) {
            path[sz - 1] = '\0';
        }
#endif

#ifdef FREESCALE_MQX
        /* remove trailing '.' */
        if (path[sz - 1] == '.') {
            path[sz - 1] = '\0';
        }
#endif
    }

    /* copy result back to 'in' buffer */
    if (WSTRLEN(in) < WSTRLEN(path)) {
        WLOG(WS_LOG_ERROR, "Fatal error cleaning path");
        WFREE(path, heap, DYNTYPE_PATH);
        return WS_BUFFER_E;
    }
    sz = (long)WSTRLEN(path);
    WMEMCPY(in, path, sz);
    in[sz] = '\0';
    WFREE(path, heap, DYNTYPE_PATH);
    return (int)sz;
}
#endif /* WOLFSSH_SFTP || WOLFSSH_SCP */


#ifdef DEBUG_WOLFSSH

#define LINE_WIDTH 16
void DumpOctetString(const byte* input, word32 inputSz)
{
    int rows = inputSz / LINE_WIDTH;
    int remainder = inputSz % LINE_WIDTH;
    int i,j;
    char text[17];
    byte c;

    for (i = 0; i < rows; i++) {
        XMEMSET(text, 0, sizeof text);
        printf("%04X: ", i * LINE_WIDTH);
        for (j = 0; j < LINE_WIDTH; j++) {
            c = input[i * LINE_WIDTH + j];
            printf("%02X ", c);
            text[j] = isprint(c) ? (char)c : '.';
        }
        printf(" %s\n", text);
    }
    if (remainder) {
        XMEMSET(text, 0, sizeof text);
        printf("%04X: ", i * LINE_WIDTH);
        for (j = 0; j < remainder; j++) {
            c = input[i * LINE_WIDTH + j];
            printf("%02X ", c);
            text[j] = isprint(c) ? c : '.';
        }
        for (; j < LINE_WIDTH; j++) {
            printf("   ");
        }
        printf(" %s\n", text);
    }
}

#endif

#ifdef WOLFSSH_SFTP

/* converts the octal input to decimal. Input is in string format i.e. 0666
 * returns the decimal value on success or negative value on failure */
int wolfSSH_oct2dec(WOLFSSH* ssh, byte* oct, word32 octSz)
{
    int ret;
    word32 i;

    if (octSz > WOLFSSH_MAX_OCTET_LEN || ssh == NULL || oct == NULL) {
        return WS_BAD_ARGUMENT;
    }

    /* convert octal string to int without mp_read_radix() */
    ret = 0;

    for (i = 0; i < octSz; i++)
    {
        if (oct[i] < '0' || oct[0] > '7') {
            ret = WS_BAD_ARGUMENT;
            break;
        }
        ret <<= 3;
        ret |= (oct[i] - '0');
    }

    return ret;
}


/* addend1 += addend2 */
void AddAssign64(word32* addend1, word32 addend2)
{
    word32 tmp = addend1[0];
    if ((addend1[0] += addend2) < tmp)
        addend1[1]++;
}

#endif /* WOLFSSH_SFTP */
