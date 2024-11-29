//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#include "task.hpp"
#include <iostream>

#include <random>   // Para generar números aleatorios
#include <vector>   // Para almacenar los AMFs seleccionados aleatoriamente

namespace nr::gnb
{

NgapAmfContext *NgapTask::selectAmf(int ueId)
{
    // Verificar si hay elementos en m_amfCtx
    if (m_amfCtx.empty()) {
        return nullptr;  // Retorna nullptr si no hay AMFs disponibles
    }

    // Crear un generador de números aleatorios
    std::random_device rd;   // Fuente de entropía (para generar números aleatorios)
    std::mt19937 gen(rd());  // Motor de números aleatorios
    std::uniform_int_distribution<> distrib(0, m_amfCtx.size() - 1);  // Distribución uniforme

    // Seleccionar un índice aleatorio dentro del rango de m_amfCtx
    int randomIndex = distrib(gen);

    // Iterar a través de m_amfCtx hasta encontrar el AMF en el índice seleccionado
    auto it = m_amfCtx.begin();
    std::advance(it, randomIndex);  // Avanzar hasta el índice aleatorio

    m_logger->debug("Selected AMF: %s, With IP: %s",  it->second->amfName, it->second->address);

    // Retornar el AMF seleccionado aleatoriamente
    return it->second;

    // for (auto &amf : m_amfCtx){
    //     m_logger->debug("AMF In For: ",  amf);
    // }

    // // todo:
    // for (auto &amf : m_amfCtx){
    //     m_logger->debug("AMF In For: ",  amf);
    //     return amf.second; // return the first one
    // }
    // return nullptr;
}

NgapAmfContext *NgapTask::selectNewAmfForReAllocation(int ueId, int initiatedAmfId, int amfSetId)
{
    // TODO an arbitrary AMF is selected for now
    return findAmfContext(initiatedAmfId);
}

} // namespace nr::gnb
