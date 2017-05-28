/////////////////////////////////////////////////////////////////////////////
// Name:        pedal.cpp
// Author:      Laurent Pugin
// Created:     2016
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "pedal.h"

//----------------------------------------------------------------------------

#include <assert.h>

//----------------------------------------------------------------------------

#include "verticalaligner.h"

namespace vrv {

//----------------------------------------------------------------------------
// Pedal
//----------------------------------------------------------------------------

Pedal::Pedal()
    : ControlElement("pedal-"), TimePointInterface(), AttColor(), AttCommonPart(), AttPedalLog(), AttPlacement()
{
    RegisterInterface(TimePointInterface::GetAttClasses(), TimePointInterface::IsInterface());
    RegisterAttClass(ATT_COLOR);
    RegisterAttClass(ATT_COMMONPART);
    RegisterAttClass(ATT_PEDALLOG);
    RegisterAttClass(ATT_PLACEMENT);

    Reset();
}

Pedal::~Pedal()
{
}

void Pedal::Reset()
{
    ControlElement::Reset();
    TimePointInterface::Reset();
    ResetColor();
    ResetCommonPart();
    ResetPedalLog();
    ResetPlacement();
}

//----------------------------------------------------------------------------
// Pedal functor methods
//----------------------------------------------------------------------------

} // namespace vrv
