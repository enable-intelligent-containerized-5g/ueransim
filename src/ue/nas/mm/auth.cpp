//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#include "mm.hpp"

#include <lib/nas/utils.hpp>
#include <ue/nas/keys.hpp>

static const bool USE_SQN_HACK = true; // TODO

namespace nr::ue
{

void NasMm::receiveAuthenticationRequest(const nas::AuthenticationRequest &msg)
{
    if (!m_usim->isValid())
    {
        m_logger->warn("Authentication request is ignored. USIM is invalid");
        return;
    }

    m_timers->t3520.start();

    if (msg.eapMessage.has_value())
        receiveAuthenticationRequestEap(msg);
    else
        receiveAuthenticationRequest5gAka(msg);
}

void NasMm::receiveAuthenticationRequestEap(const nas::AuthenticationRequest &msg)
{
    // TODO
    m_logger->err("EAP AKA' not implemented yet. Use 5G AKA instead");
    return;

    auto ueRejectionTimers = [this]() {
        m_timers->t3520.start();

        m_timers->t3510.stop();
        m_timers->t3517.stop();
        m_timers->t3521.stop();
    };

    // Read EAP-AKA' request
    auto &receivedEap = (const eap::EapAkaPrime &)*msg.eapMessage->eap;
    auto receivedRand = receivedEap.attributes.getRand();
    auto receivedMac = receivedEap.attributes.getMac();
    auto receivedAutn = receivedEap.attributes.getAutn();
    auto receivedKdf = receivedEap.attributes.getKdf();

    // m_logger->debug("received at_rand: %s", receivedRand.toHexString().c_str());
    // m_logger->debug("received at_mac: %s", receivedMac.toHexString().c_str());
    // m_logger->debug("received at_autn: %s", receivedAutn.toHexString().c_str());

    // Derive keys
    if (USE_SQN_HACK)
    {
        // Log.warning(Tag.CONFIG, "USE_SQN_HACK: %s", USE_SQN_HACK);
    }

    /*if (USE_SQN_HACK)
    {
        auto ak = calculateMilenage(OctetString::FromSpare(6), receivedRand, false).ak;
        m_usim->m_sqn = OctetString::Xor(receivedAutn.subCopy(0, 6), ak);
    }*/

    auto milenage = calculateMilenage(m_usim->m_sqnMng->getSqn(), receivedRand, false);
    auto &res = milenage.res;
    auto &ck = milenage.ck;
    auto &ik = milenage.ik;
    auto &milenageAk = milenage.ak;

    auto sqnXorAk = OctetString::Xor(m_usim->m_sqnMng->getSqn(), milenageAk);
    auto ckPrimeIkPrime =
        keys::CalculateCkPrimeIkPrime(ck, ik, keys::ConstructServingNetworkName(*m_usim->m_currentPlmn), sqnXorAk);
    auto &ckPrime = ckPrimeIkPrime.first;
    auto &ikPrime = ckPrimeIkPrime.second;

    if (!m_base->config->supi.has_value())
    {
        m_logger->err("UE has no SUPI, ignoring authentication request");
        return;
    }

    auto mk = keys::CalculateMk(ckPrime, ikPrime, m_base->config->supi.value());
    auto kaut = mk.subCopy(16, 32);

    // m_logger->debug("ueData.sqn: %s", m_usim->m_sqn.toHexString().c_str());
    // m_logger->debug("ueData.op(C): %s", m_base->config->opC.toHexString().c_str());
    // m_logger->debug("ueData.K: %s", m_base->config->key.toHexString().c_str());
    // m_logger->debug("calculated res: %s", res.toHexString().c_str());
    // m_logger->debug("calculated ck: %s", ck.toHexString().c_str());
    // m_logger->debug("calculated ik: %s", ik.toHexString().c_str());
    // m_logger->debug("calculated milenageAk: %s", milenageAk.toHexString().c_str());
    // m_logger->debug("calculated milenageMac: %s", milenageMac.toHexString().c_str());
    // m_logger->debug("calculated ckPrime: %s", ckPrime.toHexString().c_str());
    // m_logger->debug("calculated ikPrime: %s", ikPrime.toHexString().c_str());
    // m_logger->debug("calculated kaut: %s", kaut.toHexString().c_str());

    // Control received KDF
    if (receivedKdf != 1)
    {
        ueRejectionTimers();

        nas::AuthenticationReject resp;
        resp.eapMessage = nas::IEEapMessage{};
        resp.eapMessage->eap = std::make_unique<eap::EapAkaPrime>(eap::ECode::RESPONSE, receivedEap.id,
                                                                  eap::ESubType::AKA_AUTHENTICATION_REJECT);

        sendNasMessage(resp);
        return;
    }

    // Control received SSN
    {
        // todo
    }

    // Control received AUTN
    auto autnCheck = validateAutn(receivedRand, receivedAutn);
    if (autnCheck != EAutnValidationRes::OK)
    {
        eap::EapAkaPrime *eapResponse = nullptr;

        if (autnCheck == EAutnValidationRes::MAC_FAILURE)
        {
            eapResponse =
                new eap::EapAkaPrime(eap::ECode::RESPONSE, receivedEap.id, eap::ESubType::AKA_AUTHENTICATION_REJECT);
        }
        else if (autnCheck == EAutnValidationRes::SYNCHRONISATION_FAILURE)
        {
            // TODO
            // .... eapResponse = new EapAkaPrime(Eap.ECode.RESPONSE, receivedEap.id,
            // ESubType.AKA_SYNCHRONIZATION_FAILURE); eapResponse.attributes.putAuts(...);
            m_logger->err("Feature not implemented yet: SYNCHRONISATION_FAILURE in AUTN validation for EAP AKA'");
        }
        else
        {
            eapResponse = new eap::EapAkaPrime(eap::ECode::RESPONSE, receivedEap.id, eap::ESubType::AKA_CLIENT_ERROR);
            eapResponse->attributes.putClientErrorCode(0);
        }

        if (eapResponse != nullptr)
        {
            ueRejectionTimers();

            nas::AuthenticationReject resp;
            resp.eapMessage = nas::IEEapMessage{};
            resp.eapMessage->eap = std::unique_ptr<eap::Eap>(eapResponse);

            sendNasMessage(resp);
        }
        return;
    }

    // Control received AT_MAC
    auto expectedMac = keys::CalculateMacForEapAkaPrime(kaut, receivedEap);
    if (expectedMac != receivedMac)
    {
        m_logger->err("AT_MAC failure in EAP AKA'. expected: %s received: %s", expectedMac.toHexString().c_str(),
                      receivedMac.toHexString().c_str());

        ueRejectionTimers();

        auto eapResponse =
            std::make_unique<eap::EapAkaPrime>(eap::ECode::RESPONSE, receivedEap.id, eap::ESubType::AKA_CLIENT_ERROR);
        eapResponse->attributes.putClientErrorCode(0);

        nas::AuthenticationReject response;
        response.eapMessage = nas::IEEapMessage{};
        response.eapMessage->eap = std::move(eapResponse);

        sendNasMessage(response);
        return;
    }

    // Create new partial native NAS security context and continue key derivation
    auto kAusf = keys::CalculateKAusfForEapAkaPrime(mk);
    m_logger->debug("kAusf: %s", kAusf.toHexString().c_str());

    m_usim->m_nonCurrentNsCtx = std::make_unique<NasSecurityContext>();
    m_usim->m_nonCurrentNsCtx->tsc = msg.ngKSI.tsc;
    m_usim->m_nonCurrentNsCtx->ngKsi = msg.ngKSI.ksi;
    m_usim->m_rand = std::move(receivedRand);
    m_usim->m_resStar = {};
    m_usim->m_nonCurrentNsCtx->keys.kAusf = std::move(kAusf);
    m_usim->m_nonCurrentNsCtx->keys.abba = msg.abba.rawData.copy();

    keys::DeriveKeysSeafAmf(*m_base->config, *m_usim->m_currentPlmn, *m_usim->m_nonCurrentNsCtx);

    // m_logger->debug("kSeaf: %s", m_usim->m_nonCurrentNsCtx->keys.kSeaf.toHexString().c_str());
    // m_logger->debug("kAmf: %s", m_usim->m_nonCurrentNsCtx->keys.kAmf.toHexString().c_str());

    // Send Response
    {
        auto *akaPrimeResponse =
            new eap::EapAkaPrime(eap::ECode::RESPONSE, receivedEap.id, eap::ESubType::AKA_CHALLENGE);
        akaPrimeResponse->attributes.putRes(res);
        akaPrimeResponse->attributes.putMac(OctetString::FromSpare(16)); // Dummy mac for now
        akaPrimeResponse->attributes.putKdf(1);

        // Calculate and put mac value
        auto sendingMac = keys::CalculateMacForEapAkaPrime(kaut, *akaPrimeResponse);
        akaPrimeResponse->attributes.putMac(sendingMac);

        m_logger->debug("sending eap at_mac: %s", sendingMac.toHexString().c_str());

        nas::AuthenticationResponse response;
        response.eapMessage = nas::IEEapMessage{};
        response.eapMessage->eap = std::unique_ptr<eap::EapAkaPrime>(akaPrimeResponse);

        sendNasMessage(response);
    }

    // TODO (dont forget: m_nwConsecutiveAuthFailure = 0;)
}

void NasMm::receiveAuthenticationRequest5gAka(const nas::AuthenticationRequest &msg)
{
    auto sendFailure = [this](nas::EMmCause cause, std::optional<OctetString> &&auts = std::nullopt) {
        if (cause != nas::EMmCause::SYNCH_FAILURE)
            m_logger->err("Sending Authentication Failure with cause [%s]", nas::utils::EnumToString(cause));
        else
            m_logger->debug("Sending Authentication Failure due to SQN out of range");

        // Clear RAND and RES* stored in volatile memory
        m_usim->m_rand = {};
        m_usim->m_resStar = {};

        // Stop T3516 if running
        m_timers->t3516.stop();

        // Send Authentication Failure
        nas::AuthenticationFailure resp{};
        resp.mmCause.value = cause;

        if (auts.has_value())
        {
            resp.authenticationFailureParameter = nas::IEAuthenticationFailureParameter{};
            resp.authenticationFailureParameter->rawData = std::move(*auts);
        }

        sendNasMessage(resp);
    };

    // ========================== Check the received parameters syntactically ==========================

    if (!msg.authParamRAND.has_value() || !msg.authParamAUTN.has_value())
    {
        sendFailure(nas::EMmCause::SEMANTICALLY_INCORRECT_MESSAGE);
        return;
    }

    if (msg.authParamRAND->value.length() != 16 || msg.authParamAUTN->value.length() != 16)
    {
        sendFailure(nas::EMmCause::SEMANTICALLY_INCORRECT_MESSAGE);
        return;
    }

    // =================================== Check the received ngKSI ===================================

    if (msg.ngKSI.tsc == nas::ETypeOfSecurityContext::MAPPED_SECURITY_CONTEXT)
    {
        m_logger->err("Mapped security context not supported");
        sendFailure(nas::EMmCause::UNSPECIFIED_PROTOCOL_ERROR);
        return;
    }

    if (msg.ngKSI.ksi == nas::IENasKeySetIdentifier::NOT_AVAILABLE_OR_RESERVED)
    {
        m_logger->err("Invalid ngKSI value received");
        sendFailure(nas::EMmCause::UNSPECIFIED_PROTOCOL_ERROR);
        return;
    }

    if ((m_usim->m_currentNsCtx && m_usim->m_currentNsCtx->ngKsi == msg.ngKSI.ksi) ||
        (m_usim->m_nonCurrentNsCtx && m_usim->m_nonCurrentNsCtx->ngKsi == msg.ngKSI.ksi))
    {
        if (networkFailingTheAuthCheck(true))
            return;

        m_timers->t3520.start();
        sendFailure(nas::EMmCause::NGKSI_ALREADY_IN_USE);
        return;
    }

    // ============================================ Others ============================================

    auto &rand = msg.authParamRAND->value;
    auto &autn = msg.authParamAUTN->value;

    EAutnValidationRes autnCheck = EAutnValidationRes::OK;

    // If the received RAND is same with store stored RAND, bypass AUTN validation
    // NOTE: Not completely sure if this is correct and the spec meant this. But in worst case, synchronisation failure
    //  happens, and hopefully that can be restored with the normal resynchronization procedure.
    if (m_usim->m_rand != rand)
    {
        autnCheck = validateAutn(rand, autn);
        m_timers->t3516.start();
    }

    if (autnCheck == EAutnValidationRes::OK)
    {
        // Calculate milenage
        auto milenage = calculateMilenage(m_usim->m_sqnMng->getSqn(), rand, false);
        auto ckIk = OctetString::Concat(milenage.ck, milenage.ik);
        auto sqnXorAk = OctetString::Xor(m_usim->m_sqnMng->getSqn(), milenage.ak);
        auto snn = keys::ConstructServingNetworkName(*m_usim->m_currentPlmn);

        // Store the relevant parameters
        m_usim->m_rand = rand.copy();
        m_usim->m_resStar = keys::CalculateResStar(ckIk, snn, rand, milenage.res);

        // Create new partial native NAS security context and continue with key derivation
        m_usim->m_nonCurrentNsCtx = std::make_unique<NasSecurityContext>();
        m_usim->m_nonCurrentNsCtx->tsc = msg.ngKSI.tsc;
        m_usim->m_nonCurrentNsCtx->ngKsi = msg.ngKSI.ksi;
        m_usim->m_nonCurrentNsCtx->keys.kAusf = keys::CalculateKAusfFor5gAka(milenage.ck, milenage.ik, snn, sqnXorAk);
        m_usim->m_nonCurrentNsCtx->keys.abba = msg.abba.rawData.copy();

        keys::DeriveKeysSeafAmf(*m_base->config, *m_usim->m_currentPlmn, *m_usim->m_nonCurrentNsCtx);

        // Send response
        m_nwConsecutiveAuthFailure = 0;
        m_timers->t3520.stop();

        nas::AuthenticationResponse resp;
        resp.authenticationResponseParameter = nas::IEAuthenticationResponseParameter{};
        resp.authenticationResponseParameter->rawData = m_usim->m_resStar.copy();

        sendNasMessage(resp);
    }
    else if (autnCheck == EAutnValidationRes::MAC_FAILURE)
    {
        if (networkFailingTheAuthCheck(true))
            return;
        m_timers->t3520.start();
        sendFailure(nas::EMmCause::MAC_FAILURE);
    }
    else if (autnCheck == EAutnValidationRes::SYNCHRONISATION_FAILURE)
    {
        if (networkFailingTheAuthCheck(true))
            return;

        m_timers->t3520.start();

        auto milenage = calculateMilenage(m_usim->m_sqnMng->getSqn(), rand, true);
        auto auts = keys::CalculateAuts(m_usim->m_sqnMng->getSqn(), milenage.ak_r, milenage.mac_s);
        sendFailure(nas::EMmCause::SYNCH_FAILURE, std::move(auts));
    }
    else // the other case, separation bit mismatched
    {
        if (networkFailingTheAuthCheck(true))
            return;
        m_timers->t3520.start();
        sendFailure(nas::EMmCause::NON_5G_AUTHENTICATION_UNACCEPTABLE);
    }
}

void NasMm::receiveAuthenticationResult(const nas::AuthenticationResult &msg)
{
    if (msg.abba.has_value())
        m_usim->m_nonCurrentNsCtx->keys.abba = msg.abba->rawData.copy();

    if (msg.eapMessage.eap->code == eap::ECode::SUCCESS)
        receiveEapSuccessMessage(*msg.eapMessage.eap);
    else if (msg.eapMessage.eap->code == eap::ECode::FAILURE)
        receiveEapFailureMessage(*msg.eapMessage.eap);
    else
        m_logger->warn("Network sent EAP with an inconvenient type in Authentication Result, ignoring EAP IE.");
}

void NasMm::receiveAuthenticationResponse(const nas::AuthenticationResponse &msg)
{
    if (msg.eapMessage.has_value())
    {
        if (msg.eapMessage->eap->code == eap::ECode::RESPONSE)
            receiveEapResponseMessage(*msg.eapMessage->eap);
        else
            m_logger->warn("Network sent EAP with an inconvenient type in Authentication Response, ignoring EAP IE.");
    }
}

void NasMm::receiveAuthenticationReject(const nas::AuthenticationReject &msg)
{
    m_logger->err("Authentication Reject received.");

    // The RAND and RES* values stored in the ME shall be deleted and timer T3516, if running, shall be stopped
    m_usim->m_rand = {};
    m_usim->m_resStar = {};
    m_timers->t3516.stop();

    if (msg.eapMessage.has_value() && msg.eapMessage->eap->code != eap::ECode::FAILURE)
    {
        m_logger->warn("Network sent EAP with inconvenient type in AuthenticationReject, ignoring EAP IE.");
        return;
    }

    // The UE shall set the update status to 5U3 ROAMING NOT ALLOWED,
    switchUState(E5UState::U3_ROAMING_NOT_ALLOWED);
    // Delete the stored 5G-GUTI, TAI list, last visited registered TAI and ngKSI. The USIM shall be considered invalid
    // until switching off the UE or the UICC containing the USIM is removed
    m_usim->m_storedGuti = {};
    m_usim->m_lastVisitedRegisteredTai = {};
    m_usim->m_taiList = {};
    m_usim->m_currentNsCtx = {};
    m_usim->m_nonCurrentNsCtx = {};
    m_usim->invalidate();
    // The UE shall abort any 5GMM signalling procedure, stop any of the timers T3510, T3516, T3517, T3519 or T3521 (if
    // they were running) ..
    m_timers->t3510.stop();
    m_timers->t3516.stop();
    m_timers->t3517.stop();
    m_timers->t3519.stop();
    m_timers->t3521.stop();
    // .. and enter state 5GMM-DEREGISTERED.
    switchMmState(EMmState::MM_DEREGISTERED, EMmSubState::MM_DEREGISTERED_NA);
}

void NasMm::receiveEapSuccessMessage(const eap::Eap &eap)
{
    // do nothing
}

void NasMm::receiveEapFailureMessage(const eap::Eap &eap)
{
    m_logger->err("EAP failure received. Deleting non-current NAS security context");
    m_usim->m_nonCurrentNsCtx = {};
}

void NasMm::receiveEapResponseMessage(const eap::Eap &eap)
{
    if (eap.eapType == eap::EEapType::EAP_AKA_PRIME)
    {
        // TODO
    }
    else
    {
        m_logger->err("Unhandled EAP Response message type");
    }
}

EAutnValidationRes NasMm::validateAutn(const OctetString &rand, const OctetString &autn)
{
    // Decode AUTN
    OctetString receivedSQNxorAK = autn.subCopy(0, 6);
    OctetString receivedAMF = autn.subCopy(6, 2);
    OctetString receivedMAC = autn.subCopy(8, 8);

    // Check the separation bit
    if (receivedAMF.get(0).bit(7) != 1)
    {
        m_logger->err("AUTN validation SEP-BIT failure. expected: 1, received: 0");
        return EAutnValidationRes::AMF_SEPARATION_BIT_FAILURE;
    }

    // Derive AK and MAC
    auto milenage = calculateMilenage(m_usim->m_sqnMng->getSqn(), rand, false);
    OctetString receivedSQN = OctetString::Xor(receivedSQNxorAK, milenage.ak);

    // Verify that the received sequence number SQN is in the correct range
    if (!m_usim->m_sqnMng->checkSqn(receivedSQN))
        return EAutnValidationRes::SYNCHRONISATION_FAILURE;

    // Re-execute the milenage calculation (if case of sqn is changed with the received value)
    milenage = calculateMilenage(m_usim->m_sqnMng->getSqn(), rand, false);

    // Check MAC
    if (receivedMAC != milenage.mac_a)
    {
        m_logger->err("AUTN validation MAC mismatch. expected [%s] received [%s]", milenage.mac_a.toHexString().c_str(),
                      receivedMAC.toHexString().c_str());
        return EAutnValidationRes::MAC_FAILURE;
    }

    return EAutnValidationRes::OK;
}

crypto::milenage::Milenage NasMm::calculateMilenage(const OctetString &sqn, const OctetString &rand, bool dummyAmf)
{
    OctetString amf = dummyAmf ? OctetString::FromSpare(2) : m_base->config->amf.copy();

    if (m_base->config->opType == OpType::OPC)
        return crypto::milenage::Calculate(m_base->config->opC, m_base->config->key, rand, sqn, amf);

    OctetString opc = crypto::milenage::CalculateOpC(m_base->config->opC, m_base->config->key);
    return crypto::milenage::Calculate(opc, m_base->config->key, rand, sqn, amf);
}

bool NasMm::networkFailingTheAuthCheck(bool hasChance)
{
    if (hasChance && m_nwConsecutiveAuthFailure++ < 3)
        return false;

    // NOTE: Normally if we should check if the UE has an emergency. If it has, it should consider as network passed the
    //  auth check, instead of performing the actions in the following lines. But it's difficult to maintain and
    //  implement this behaviour. Therefore we would expect other solutions for an emergency case. Such as
    //  - Network initiates a Security Mode Command with IA0 and EA0
    //  - UE performs emergency registration after releasing the connection
    // END

    m_logger->err("Network failing the authentication check");
    localReleaseConnection();
    // TODO: treat the active cell as barred
    return true;
}

} // namespace nr::ue