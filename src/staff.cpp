/////////////////////////////////////////////////////////////////////////////
// Name:        staff.cpp
// Author:      Laurent Pugin
// Created:     2005
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "staff.h"

//----------------------------------------------------------------------------

#include <cassert>
#include <vector>

//----------------------------------------------------------------------------

#include "comparison.h"
#include "doc.h"
#include "editorial.h"
#include "functor.h"
#include "functorparams.h"
#include "hairpin.h"
#include "keysig.h"
#include "layer.h"
#include "measure.h"
#include "note.h"
#include "page.h"
#include "staffdef.h"
#include "syl.h"
#include "system.h"
#include "timeinterface.h"
#include "tuning.h"
#include "verse.h"
#include "vrv.h"
#include "zone.h"

namespace vrv {

//----------------------------------------------------------------------------
// Staff
//----------------------------------------------------------------------------

static const ClassRegistrar<Staff> s_factory("staff", STAFF);

Staff::Staff(int n)
    : Object(STAFF, "staff-"), FacsimileInterface(), AttCoordY1(), AttNInteger(), AttTyped(), AttVisibility()
{
    this->RegisterAttClass(ATT_COORDY1);
    this->RegisterAttClass(ATT_NINTEGER);
    this->RegisterAttClass(ATT_TYPED);
    this->RegisterAttClass(ATT_VISIBILITY);
    this->RegisterInterface(FacsimileInterface::GetAttClasses(), FacsimileInterface::IsInterface());

    this->Reset();
    this->SetN(n);
}

Staff::~Staff() {}

void Staff::Reset()
{
    Object::Reset();
    FacsimileInterface::Reset();
    this->ResetCoordY1();
    this->ResetNInteger();
    this->ResetTyped();
    this->ResetVisibility();

    m_yAbs = VRV_UNSET;

    m_drawingStaffSize = 100;
    m_drawingLines = 5;
    m_drawingNotationType = NOTATIONTYPE_NONE;
    m_staffAlignment = NULL;
    m_timeSpanningElements.clear();
    m_drawingStaffDef = NULL;
    m_drawingTuning = NULL;

    ClearLedgerLines();
}

void Staff::CloneReset()
{
    Object::CloneReset();

    m_drawingStaffSize = 100;
    m_drawingLines = 5;
    m_drawingNotationType = NOTATIONTYPE_NONE;
    m_staffAlignment = NULL;
    m_timeSpanningElements.clear();
    m_drawingStaffDef = NULL;
    m_drawingTuning = NULL;
}

void Staff::ClearLedgerLines()
{
    m_ledgerLinesAbove.clear();
    m_ledgerLinesBelow.clear();
    m_ledgerLinesAboveCue.clear();
    m_ledgerLinesBelowCue.clear();
}

bool Staff::IsSupportedChild(Object *child)
{
    if (child->Is(LAYER)) {
        Layer *layer = vrv_cast<Layer *>(child);
        assert(layer);
        if (layer && !layer->HasN()) {
            // This is not 100% safe if we have a <app> and <rdg> with more than
            // one layer as a previous child.
            layer->SetN(this->GetChildCount(LAYER) + 1);
        }
    }
    else if (child->IsEditorialElement()) {
        assert(dynamic_cast<EditorialElement *>(child));
    }
    else {
        return false;
    }
    return true;
}

int Staff::GetDrawingX() const
{
    if (this->HasFacs()) {
        const Doc *doc = vrv_cast<const Doc *>(this->GetFirstAncestor(DOC));
        assert(doc);
        if (doc->GetType() == Facs) {
            return FacsimileInterface::GetDrawingX();
        }
    }
    return Object::GetDrawingX();
}

int Staff::GetDrawingY() const
{
    if (this->HasFacs()) {
        const Doc *doc = vrv_cast<const Doc *>(this->GetFirstAncestor(DOC));
        assert(DOC);
        if (doc->GetType() == Facs) {
            return FacsimileInterface::GetDrawingY();
        }
    }

    if (m_yAbs != VRV_UNSET) return m_yAbs;

    if (!m_staffAlignment) return 0;

    if (m_cachedDrawingY != VRV_UNSET) return m_cachedDrawingY;

    const System *system = vrv_cast<const System *>(this->GetFirstAncestor(SYSTEM));
    assert(system);

    m_cachedDrawingY = system->GetDrawingY() + m_staffAlignment->GetYRel();
    return m_cachedDrawingY;
}

double Staff::GetDrawingRotate() const
{
    if (this->HasFacs()) {
        const Doc *doc = vrv_cast<const Doc *>(this->GetFirstAncestor(DOC));
        assert(doc);
        if (doc->GetType() == Facs) {
            return FacsimileInterface::GetDrawingRotate();
        }
    }
    return 0;
}

void Staff::AdjustDrawingStaffSize()
{
    if (this->HasFacs()) {
        Doc *doc = vrv_cast<Doc *>(this->GetFirstAncestor(DOC));
        assert(doc);
        if (doc->GetType() == Facs) {
            double rotate = this->GetDrawingRotate();
            Zone *zone = this->GetZone();
            assert(zone);
            int yDiff
                = zone->GetLry() - zone->GetUly() - (zone->GetLrx() - zone->GetUlx()) * tan(abs(rotate) * M_PI / 180.0);
            m_drawingStaffSize = 100 * yDiff / (doc->GetOptions()->m_unit.GetValue() * 2 * (m_drawingLines - 1));
        }
    }
}

int Staff::GetDrawingStaffNotationSize() const
{
    return (this->IsTablature()) ? m_drawingStaffSize / TABLATURE_STAFF_RATIO : m_drawingStaffSize;
}

bool Staff::DrawingIsVisible() const
{
    const System *system = vrv_cast<const System *>(this->GetFirstAncestor(SYSTEM));
    assert(system);
    assert(system->GetDrawingScoreDef());

    const StaffDef *staffDef = system->GetDrawingScoreDef()->GetStaffDef(this->GetN());
    assert(staffDef);
    return (staffDef->GetDrawingVisibility() != OPTIMIZATION_HIDDEN);
}

bool Staff::IsMensural() const
{
    bool isMensural
        = (m_drawingNotationType == NOTATIONTYPE_mensural || m_drawingNotationType == NOTATIONTYPE_mensural_white
            || m_drawingNotationType == NOTATIONTYPE_mensural_black);
    return isMensural;
}

bool Staff::IsNeume() const
{
    bool isNeume = (m_drawingNotationType == NOTATIONTYPE_neume);
    return isNeume;
}

bool Staff::IsTablature() const
{
    bool isTablature = (m_drawingNotationType == NOTATIONTYPE_tab || m_drawingNotationType == NOTATIONTYPE_tab_guitar
        || m_drawingNotationType == NOTATIONTYPE_tab_lute_italian
        || m_drawingNotationType == NOTATIONTYPE_tab_lute_french
        || m_drawingNotationType == NOTATIONTYPE_tab_lute_german);
    return isTablature;
}

bool Staff::IsTabWithStemsOutside() const
{
    if (!m_drawingStaffDef) return false;
    // Temporary implementation looking at staffDef@type
    return (!this->IsTabGuitar() || !m_drawingStaffDef->HasType() || m_drawingStaffDef->GetType() != "stems.within");
}

int Staff::CalcPitchPosYRel(const Doc *doc, int loc) const
{
    assert(doc);

    // the staff loc offset is based on the number of lines: 0 with 1 line, 2 with 2, etc
    int staffLocOffset = (m_drawingLines - 1) * 2;
    return (loc - staffLocOffset) * doc->GetDrawingUnit(m_drawingStaffSize);
}

void Staff::AddLedgerLineAbove(int count, int left, int right, int extension, bool cueSize)
{
    this->AddLedgerLines(cueSize ? m_ledgerLinesAboveCue : m_ledgerLinesAbove, count, left, right, extension);
}

void Staff::AddLedgerLineBelow(int count, int left, int right, int extension, bool cueSize)
{
    this->AddLedgerLines(cueSize ? m_ledgerLinesBelowCue : m_ledgerLinesBelow, count, left, right, extension);
}

void Staff::AddLedgerLines(ArrayOfLedgerLines &lines, int count, int left, int right, int extension)
{
    assert(left < right);

    if ((int)lines.size() < count) lines.resize(count);
    int i = 0;
    for (i = 0; i < count; ++i) {
        lines.at(i).AddDash(left, right, extension);
    }
}

void Staff::SetFromFacsimile(Doc *doc)
{
    if (!this->HasFacs()) return;
    if (this->GetZone() == NULL) {
        assert(doc);
        Zone *zone = doc->GetFacsimile()->FindZoneByID(this->GetFacs());
        assert(zone);
        this->AttachZone(zone);
    }
    this->AdjustDrawingStaffSize();
}

bool Staff::IsOnStaffLine(int y, const Doc *doc) const
{
    assert(doc);

    return ((y - this->GetDrawingY()) % (2 * doc->GetDrawingUnit(m_drawingStaffSize)) == 0);
}

int Staff::GetNearestInterStaffPosition(int y, const Doc *doc, data_STAFFREL place) const
{
    assert(doc);

    int yPos = y - this->GetDrawingY();
    int distance = yPos % doc->GetDrawingUnit(m_drawingStaffSize);
    if (place == STAFFREL_above) {
        if (distance > 0) distance = doc->GetDrawingUnit(m_drawingStaffSize) - distance;
        return y - distance + doc->GetDrawingUnit(m_drawingStaffSize);
    }
    else {
        if (distance < 0) distance = doc->GetDrawingUnit(m_drawingStaffSize) + distance;
        return y - distance - doc->GetDrawingUnit(m_drawingStaffSize);
    }
}

//----------------------------------------------------------------------------
// LedgerLine
//----------------------------------------------------------------------------

LedgerLine::LedgerLine()
{
    this->Reset();
}

LedgerLine::~LedgerLine() {}

void LedgerLine::Reset()
{
    m_dashes.clear();
}

void LedgerLine::AddDash(int left, int right, int extension)
{
    assert(left < right);

    std::list<std::pair<int, int>>::iterator iter;

    // First add the dash
    for (iter = m_dashes.begin(); iter != m_dashes.end(); ++iter) {
        if (iter->first > left) break;
    }
    m_dashes.insert(iter, { left, right });

    // Merge dashes which overlap by more than 1.5 extensions
    // => Dashes belonging to the same chord overlap at least by two extensions and will get merged
    // => Overlapping dashes of adjacent notes will not get merged
    std::list<std::pair<int, int>>::iterator previous = m_dashes.begin();
    iter = m_dashes.begin();
    ++iter;
    while (iter != m_dashes.end()) {
        if (previous->second > iter->first + 1.5 * extension) {
            previous->second = std::max(iter->second, previous->second);
            iter = m_dashes.erase(iter);
        }
        else {
            previous = iter;
            ++iter;
        }
    }
}

//----------------------------------------------------------------------------
// Staff functor methods
//----------------------------------------------------------------------------

FunctorCode Staff::Accept(MutableFunctor &functor)
{
    return functor.VisitStaff(this);
}

FunctorCode Staff::Accept(ConstFunctor &functor) const
{
    return functor.VisitStaff(this);
}

FunctorCode Staff::AcceptEnd(MutableFunctor &functor)
{
    return functor.VisitStaffEnd(this);
}

FunctorCode Staff::AcceptEnd(ConstFunctor &functor) const
{
    return functor.VisitStaffEnd(this);
}

int Staff::ConvertToCastOffMensural(FunctorParams *functorParams)
{
    ConvertToCastOffMensuralParams *params = vrv_params_cast<ConvertToCastOffMensuralParams *>(functorParams);
    assert(params);

    params->m_targetStaff = new Staff(*this);
    params->m_targetStaff->ClearChildren();
    params->m_targetStaff->CloneReset();
    // Keep the xml:id of the staff in the first staff segment
    params->m_targetStaff->SwapID(this);
    assert(params->m_targetMeasure);
    params->m_targetMeasure->AddChild(params->m_targetStaff);

    return FUNCTOR_CONTINUE;
}

int Staff::ResetVerticalAlignment(FunctorParams *functorParams)
{
    m_staffAlignment = NULL;

    ClearLedgerLines();

    return FUNCTOR_CONTINUE;
}

int Staff::ApplyPPUFactor(FunctorParams *functorParams)
{
    ApplyPPUFactorParams *params = vrv_params_cast<ApplyPPUFactorParams *>(functorParams);
    assert(params);

    if (m_yAbs != VRV_UNSET) m_yAbs /= params->m_page->GetPPUFactor();

    return FUNCTOR_CONTINUE;
}

int Staff::AlignHorizontally(FunctorParams *functorParams)
{
    AlignHorizontallyParams *params = vrv_params_cast<AlignHorizontallyParams *>(functorParams);
    assert(params);

    assert(m_drawingStaffDef);

    if (m_drawingStaffDef->HasNotationtype()) {
        params->m_notationType = m_drawingStaffDef->GetNotationtype();
    }
    else {
        params->m_notationType = NOTATIONTYPE_cmn;
    }
    Measure *parentMeasure = vrv_cast<Measure *>(this->GetFirstAncestor(MEASURE));
    if (parentMeasure) m_drawingStaffDef->AlternateCurrentMeterSig(parentMeasure);

    return FUNCTOR_CONTINUE;
}

int Staff::AlignVertically(FunctorParams *functorParams)
{
    AlignVerticallyParams *params = vrv_params_cast<AlignVerticallyParams *>(functorParams);
    assert(params);

    if (!this->DrawingIsVisible()) {
        return FUNCTOR_SIBLINGS;
    }

    params->m_staffN = this->GetN();

    // this gets (or creates) the measureAligner for the measure
    StaffAlignment *alignment = params->m_systemAligner->GetStaffAlignment(params->m_staffIdx, this, params->m_doc);

    assert(alignment);

    // Set the pointer of the m_alignment
    m_staffAlignment = alignment;

    std::vector<Object *>::const_iterator verseIterator
        = std::find_if(m_timeSpanningElements.begin(), m_timeSpanningElements.end(), ObjectComparison(VERSE));
    if (verseIterator != m_timeSpanningElements.end()) {
        Verse *v = vrv_cast<Verse *>(*verseIterator);
        assert(v);
        alignment->AddVerseN(v->GetN());
    }

    // add verse number to alignment in case there are spanning SYL elements but there is no verse number already - this
    // generally happens with verses spanning over several systems which results in invalid placement of connector lines
    std::vector<Object *>::const_iterator sylIterator
        = std::find_if(m_timeSpanningElements.begin(), m_timeSpanningElements.end(), ObjectComparison(SYL));
    if (sylIterator != m_timeSpanningElements.end()) {
        Verse *verse = vrv_cast<Verse *>((*sylIterator)->GetFirstAncestor(VERSE));
        if (verse) {
            const int verseNumber = verse->GetN();
            const bool verseCollapse = params->m_doc->GetOptions()->m_lyricVerseCollapse.GetValue();
            if (!alignment->GetVersePosition(verseNumber, verseCollapse)) {
                alignment->AddVerseN(verseNumber);
            }
        }
    }

    // for next staff
    params->m_staffIdx++;

    return FUNCTOR_CONTINUE;
}

int Staff::PrepareStaffCurrentTimeSpanning(FunctorParams *functorParams)
{
    PrepareStaffCurrentTimeSpanningParams *params
        = vrv_params_cast<PrepareStaffCurrentTimeSpanningParams *>(functorParams);
    assert(params);

    std::vector<Object *>::iterator iter = params->m_timeSpanningElements.begin();
    while (iter != params->m_timeSpanningElements.end()) {
        TimeSpanningInterface *interface = (*iter)->GetTimeSpanningInterface();
        assert(interface);
        Measure *currentMeasure = vrv_cast<Measure *>(this->GetFirstAncestor(MEASURE));
        assert(currentMeasure);
        // We need to make sure we are in the next measure (and not just a staff below because of some cross staff
        // notation
        if ((interface->GetStartMeasure() != currentMeasure) && (interface->IsOnStaff(this->GetN()))) {
            m_timeSpanningElements.push_back(*iter);
        }
        ++iter;
    }
    return FUNCTOR_CONTINUE;
}

int Staff::CastOffEncoding(FunctorParams *functorParams)
{
    // Staff alignments must be reset, otherwise they would dangle whenever they belong to a deleted system
    m_staffAlignment = NULL;
    return FUNCTOR_SIBLINGS;
}

int Staff::ResetData(FunctorParams *functorParams)
{
    m_timeSpanningElements.clear();
    ClearLedgerLines();
    return FUNCTOR_CONTINUE;
}

int Staff::InitOnsetOffset(FunctorParams *functorParams)
{
    InitOnsetOffsetParams *params = vrv_params_cast<InitOnsetOffsetParams *>(functorParams);
    assert(params);

    assert(m_drawingStaffDef);

    if (m_drawingStaffDef->HasNotationtype()) {
        params->m_notationType = m_drawingStaffDef->GetNotationtype();
    }
    else {
        params->m_notationType = NOTATIONTYPE_cmn;
    }

    return FUNCTOR_CONTINUE;
}

int Staff::CalcStem(FunctorParams *)
{
    ListOfObjects layers = this->FindAllDescendantsByType(LAYER, false);
    if (layers.empty()) {
        return FUNCTOR_CONTINUE;
    }

    // Not more than one layer - drawing stem dir remains unset unless there is cross-staff content
    if (layers.size() < 2) {
        Layer *layer = vrv_cast<Layer *>(layers.front());
        assert(layer);
        if (layer->HasCrossStaffFromBelow()) {
            layer->SetDrawingStemDir(STEMDIRECTION_up);
        }
        else if (layer->HasCrossStaffFromAbove()) {
            layer->SetDrawingStemDir(STEMDIRECTION_down);
        }
        return FUNCTOR_CONTINUE;
    }

    // Detecting empty layers (empty layers can also have @sameas) which have to be ignored for stem direction
    IsEmptyComparison isEmptyElement(LAYER);
    ListOfObjects emptyLayers;
    this->FindAllDescendantsByComparison(&emptyLayers, &isEmptyElement);

    // We have only one layer (or less) with content - drawing stem dir remains unset
    if ((layers.size() < 3) && (emptyLayers.size() > 0)) {
        return FUNCTOR_CONTINUE;
    }

    if (!emptyLayers.empty()) {
        ListOfObjects nonEmptyLayers;
        // not need to sort since it already sorted
        std::set_difference(layers.begin(), layers.end(), emptyLayers.begin(), emptyLayers.end(),
            std::inserter(nonEmptyLayers, nonEmptyLayers.begin()));
        layers = nonEmptyLayers;
    }

    for (auto &object : layers) {
        // Alter stem direction between even and odd numbered layers
        Layer *layer = dynamic_cast<Layer *>(object);
        layer->SetDrawingStemDir(layer->GetN() % 2 ? STEMDIRECTION_up : STEMDIRECTION_down);
    }

    return FUNCTOR_CONTINUE;
}

int Staff::AdjustSylSpacing(FunctorParams *functorParams)
{
    AdjustSylSpacingParams *params = vrv_params_cast<AdjustSylSpacingParams *>(functorParams);
    assert(params);

    // Set the staff size for this pass
    params->m_staffSize = m_drawingStaffSize;

    return FUNCTOR_CONTINUE;
}

int Staff::GenerateMIDI(FunctorParams *functorParams)
{
    GenerateMIDIParams *params = vrv_params_cast<GenerateMIDIParams *>(functorParams);
    assert(params);

    params->m_expandedNotes.clear();

    return FUNCTOR_CONTINUE;
}

int Staff::Transpose(FunctorParams *functorParams)
{
    TransposeParams *params = vrv_params_cast<TransposeParams *>(functorParams);
    assert(params);

    if (params->m_transposeToSoundingPitch) {
        int transposeInterval = 0;
        if (this->HasN() && (params->m_transposeIntervalForStaffN.count(this->GetN()) > 0)) {
            transposeInterval = params->m_transposeIntervalForStaffN.at(this->GetN());
        }
        params->m_transposer->SetTransposition(transposeInterval);
    }

    return FUNCTOR_CONTINUE;
}

} // namespace vrv
