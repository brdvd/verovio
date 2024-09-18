/////////////////////////////////////////////////////////////////////////////
// Name:        iocmme.cpp
// Author:      Laurent Pugin
// Created:     2024
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "iocmme.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

//----------------------------------------------------------------------------

#include "barline.h"
#include "clef.h"
#include "doc.h"
#include "dot.h"
#include "keyaccid.h"
#include "keysig.h"
#include "label.h"
#include "layer.h"
#include "mdiv.h"
#include "measure.h"
#include "mensur.h"
#include "note.h"
#include "rest.h"
#include "score.h"
#include "section.h"
#include "staff.h"
#include "staffdef.h"
#include "staffgrp.h"
#include "syl.h"
#include "text.h"
#include "verse.h"
#include "vrv.h"

//----------------------------------------------------------------------------

namespace vrv {

//----------------------------------------------------------------------------
// CmmeInput
//----------------------------------------------------------------------------

CmmeInput::CmmeInput(Doc *doc) : Input(doc)
{
    m_score = NULL;
    m_currentSection = NULL;
    m_currentLayer = NULL;
    m_currentNote = NULL;
    m_isInSyllable = false;
    m_mensInfo = NULL;
}

CmmeInput::~CmmeInput() {}

//////////////////////////////////////////////////////////////////////////

bool CmmeInput::Import(const std::string &cmme)
{
    try {
        m_doc->Reset();
        m_doc->SetType(Raw);
        pugi::xml_document doc;
        doc.load_string(cmme.c_str(), (pugi::parse_comments | pugi::parse_default) & ~pugi::parse_eol);
        pugi::xml_node root = doc.first_child();

        // The mDiv
        Mdiv *mdiv = new Mdiv();
        mdiv->m_visibility = Visible;
        m_doc->AddChild(mdiv);
        // The score
        m_score = new Score();
        mdiv->AddChild(m_score);

        // We asssume that there is always as many Voice elements than given in NumVoices
        pugi::xpath_node_set voices = root.select_nodes("/Piece/VoiceData/Voice");
        for (pugi::xpath_node voiceNode : voices) {
            m_numVoices++;
            // Get the voice name if any
            std::string name = ChildAsString(voiceNode.node(), "Name");
            m_voices.push_back(name);
        }
        // Allocatate the mensural infos initialized with everything binary
        m_mensInfos.resize(m_numVoices);

        pugi::xpath_node_set musicSections = root.select_nodes("/Piece/MusicSection/*");

        for (pugi::xpath_node musicSectionNode : musicSections) {
            CreateSection(musicSectionNode.node());
        }

        // add minimal scoreDef
        StaffGrp *staffGrp = new StaffGrp();
        GrpSym *grpSym = new GrpSym();
        grpSym->SetSymbol(staffGroupingSym_SYMBOL_bracket);
        staffGrp->AddChild(grpSym);
        for (int i = 0; i < m_numVoices; ++i) {
            StaffDef *staffDef = new StaffDef();
            staffDef->SetN(i + 1);
            staffDef->SetLines(5);
            staffDef->SetNotationtype(NOTATIONTYPE_mensural);
            staffGrp->AddChild(staffDef);
            // Label
            if (!m_voices.at(i).empty()) {
                Label *label = new Label();
                Text *text = new Text();
                text->SetText(UTF8to32(m_voices.at(i)));
                label->AddChild(text);
                staffDef->AddChild(label);
            }
            // Default mensur with everything binary in CMME
            Mensur *mensur = new Mensur();
            mensur->SetProlatio(PROLATIO_2);
            mensur->SetTempus(TEMPUS_2);
            mensur->SetModusminor(MODUSMINOR_2);
            mensur->SetModusmaior(MODUSMAIOR_2);
            staffDef->AddChild(mensur);
        }

        m_score->GetScoreDef()->AddChild(staffGrp);

        m_doc->ConvertToPageBasedDoc();
    }
    catch (char *str) {
        LogError("%s", str);
        return false;
    }

    return true;
}

void CmmeInput::CreateSection(pugi::xml_node musicSectionNode)
{
    assert(m_score);

    std::string sectionType = musicSectionNode.name();

    // Create a new section
    Section *section = new Section();
    // Add the section type (MensuralMusic, Plainchant) to `@type`
    section->SetType(sectionType);
    m_score->AddChild(section);

    // Set the current section to an invisible unmeasured measure
    m_currentSection = new Measure(UNMEASURED, 1);
    section->AddChild(m_currentSection);

    // Loop through the number of voices and parse Voice or create an empty staff if not given
    for (int i = 0; i < m_numVoices; ++i) {
        std::string xpath = StringFormat("./Voice[VoiceNum[text()='%d']]", i + 1);
        pugi::xpath_node voice = musicSectionNode.select_node(xpath.c_str());
        if (voice) {
            CreateStaff(voice.node());
        }
        else {
            Staff *staff = new Staff(i + 1);
            staff->SetVisible(BOOLEAN_false);
            m_currentSection->AddChild(staff);
        }
    }
}

void CmmeInput::CreateStaff(pugi::xml_node voiceNode)
{
    assert(m_currentSection);

    int numVoice = this->ChildAsInt(voiceNode, "VoiceNum");

    Staff *staff = new Staff(numVoice);
    m_currentLayer = new Layer();
    m_currentLayer->SetN(1);

    // (Re)-set the current mens info to the corresponding voice
    m_mensInfo = &m_mensInfos.at(numVoice - 1);
    // Reset the syllable position
    m_isInSyllable = false;

    // Loop through the event lists
    pugi::xpath_node_set events = voiceNode.select_nodes("./EventList/*");
    for (pugi::xpath_node event : events) {
        pugi::xml_node eventNode = event.node();
        std::string name = eventNode.name();
        if (name == "Clef") {
            if (this->IsClef(eventNode)) {
                CreateClef(eventNode);
            }
            else {
                CreateKeySig(eventNode);
            }
        }
        else if (name == "Dot") {
            CreateDot(eventNode);
        }
        else if (name == "Mensuration") {
            CreateMensuration(eventNode);
        }
        else if (name == "Note") {
            CreateNote(eventNode);
        }
        else if (name == "OriginalText") {
            CreateOriginalText(eventNode);
        }
        else if (name == "Rest") {
            CreateRest(eventNode);
        }
        else {
            LogWarning("Unsupported event '%s'", name.c_str());
        }
    }

    staff->AddChild(m_currentLayer);
    m_currentSection->AddChild(staff);
}

void CmmeInput::CreateClef(pugi::xml_node clefNode)
{
    static const std::map<std::string, data_CLEFSHAPE> shapeMap{
        { "C", CLEFSHAPE_C }, //
        { "F", CLEFSHAPE_F }, //
        { "G", CLEFSHAPE_G }, //
        { "Frnd", CLEFSHAPE_F }, //
        { "Fsqr", CLEFSHAPE_F }, //
    };

    assert(m_currentLayer);

    Clef *clef = new Clef();
    int staffLoc = this->ChildAsInt(clefNode, "StaffLoc");
    staffLoc = (staffLoc + 1) / 2;
    clef->SetLine(staffLoc);

    std::string appearance = this->ChildAsString(clefNode, "Appearance");
    // Default clef to C
    data_CLEFSHAPE shape = shapeMap.contains(appearance) ? shapeMap.at(appearance) : CLEFSHAPE_C;
    clef->SetShape(shape);

    m_currentLayer->AddChild(clef);

    return;
}

void CmmeInput::CreateDot(pugi::xml_node dotNode)
{
    assert(m_currentLayer);

    Dot *dot = new Dot();
    m_currentLayer->AddChild(dot);

    return;
}

void CmmeInput::CreateKeySig(pugi::xml_node keyNode)
{
    assert(m_currentLayer);

    static const std::map<std::string, data_ACCIDENTAL_WRITTEN> shapeMap{
        { "Bmol", ACCIDENTAL_WRITTEN_f }, //
        { "BmolDouble", ACCIDENTAL_WRITTEN_f }, //
        { "Bqua", ACCIDENTAL_WRITTEN_n }, //
        { "Diesis", ACCIDENTAL_WRITTEN_s }, //
    };

    KeySig *keysig = new KeySig();
    KeyAccid *keyaccid = new KeyAccid();
    std::string appearance = this->ChildAsString(keyNode, "Appearance");
    data_ACCIDENTAL_WRITTEN accid = shapeMap.contains(appearance) ? shapeMap.at(appearance) : ACCIDENTAL_WRITTEN_f;
    keyaccid->SetAccid(accid);

    static const std::map<std::string, data_PITCHNAME> pitchMap{
        { "C", PITCHNAME_c }, //
        { "D", PITCHNAME_d }, //
        { "E", PITCHNAME_e }, //
        { "F", PITCHNAME_f }, //
        { "G", PITCHNAME_g }, //
        { "A", PITCHNAME_a }, //
        { "B", PITCHNAME_b } //
    };

    std::string step = this->ChildAsString(keyNode, "Pitch/LetterName");
    // Default pitch to C
    data_PITCHNAME pname = pitchMap.contains(step) ? pitchMap.at(step) : PITCHNAME_c;
    keyaccid->SetPname(pname);

    int oct = this->ChildAsInt(keyNode, "Pitch/OctaveNum");
    if ((pname != PITCHNAME_a) && (pname != PITCHNAME_b)) oct += 1;
    keyaccid->SetOct(oct);

    int staffLoc = this->ChildAsInt(keyNode, "StaffLoc");
    keyaccid->SetLoc(staffLoc - 1);

    m_currentLayer->AddChild(keysig);
    keysig->AddChild(keyaccid);
}

void CmmeInput::CreateMensuration(pugi::xml_node mensurationNode)
{
    assert(m_currentLayer);
    assert(m_mensInfo);

    pugi::xml_node mensInfo = mensurationNode.child("MensInfo");
    if (mensInfo) {
        m_mensInfo->prolatio = this->ChildAsInt(mensInfo, "Prolatio");
        m_mensInfo->tempus = this->ChildAsInt(mensInfo, "Tempus");
        m_mensInfo->modusminor = this->ChildAsInt(mensInfo, "ModusMinor");
        m_mensInfo->modusmaior = this->ChildAsInt(mensInfo, "ModusMaior");
    }

    Mensur *mensur = new Mensur();
    data_PROLATIO prolatio = (m_mensInfo->prolatio == 3) ? PROLATIO_3 : PROLATIO_2;
    mensur->SetProlatio(prolatio);
    data_TEMPUS tempus = (m_mensInfo->tempus == 3) ? TEMPUS_3 : TEMPUS_2;
    mensur->SetTempus(tempus);
    data_MODUSMINOR modusminor = (m_mensInfo->modusminor == 3) ? MODUSMINOR_3 : MODUSMINOR_2;
    mensur->SetModusminor(modusminor);
    data_MODUSMAIOR modusmaior = (m_mensInfo->modusmaior == 3) ? MODUSMAIOR_3 : MODUSMAIOR_2;
    mensur->SetModusmaior(modusmaior);
    data_MENSURATIONSIGN sign = (m_mensInfo->tempus == 3) ? MENSURATIONSIGN_O : MENSURATIONSIGN_C;
    mensur->SetSign(sign);
    data_BOOLEAN dot = (m_mensInfo->prolatio == 3) ? BOOLEAN_true : BOOLEAN_false;
    mensur->SetDot(dot);

    m_currentLayer->AddChild(mensur);

    return;
}

void CmmeInput::CreateNote(pugi::xml_node noteNode)
{
    static const std::map<std::string, data_PITCHNAME> pitchMap{
        { "C", PITCHNAME_c }, //
        { "D", PITCHNAME_d }, //
        { "E", PITCHNAME_e }, //
        { "F", PITCHNAME_f }, //
        { "G", PITCHNAME_g }, //
        { "A", PITCHNAME_a }, //
        { "B", PITCHNAME_b } //
    };

    assert(m_currentLayer);

    Note *note = new Note();
    std::string step = this->ChildAsString(noteNode, "LetterName");
    // Default pitch to C
    data_PITCHNAME pname = pitchMap.contains(step) ? pitchMap.at(step) : PITCHNAME_c;
    note->SetPname(pname);

    int num;
    int numbase;
    data_DURATION duration = this->ReadDuration(noteNode, num, numbase);
    note->SetDur(duration);
    if (num != VRV_UNSET && numbase != VRV_UNSET) {
        note->SetNumbase(num);
        note->SetNum(numbase);
    }

    int oct = this->ChildAsInt(noteNode, "OctaveNum");
    if ((pname != PITCHNAME_a) && (pname != PITCHNAME_b)) oct += 1;
    note->SetOct(oct);

    if (noteNode.child("Colored")) {
        note->SetColored(BOOLEAN_true);
    }

    if (noteNode.child("ModernText")) {
        m_currentNote = note;
        CreateVerse(noteNode.child("ModernText"));
        m_currentNote = NULL;
    }

    m_currentLayer->AddChild(note);

    return;
}

void CmmeInput::CreateOriginalText(pugi::xml_node originalTextNode)
{
    return;
}

void CmmeInput::CreateRest(pugi::xml_node restNode)
{
    assert(m_currentLayer);

    Rest *rest = new Rest();
    int num;
    int numbase;
    data_DURATION duration = this->ReadDuration(restNode, num, numbase);
    rest->SetDur(duration);
    if (num != VRV_UNSET && numbase != VRV_UNSET) {
        rest->SetNumbase(num);
        rest->SetNum(numbase);
    }

    m_currentLayer->AddChild(rest);

    return;
}

void CmmeInput::CreateVerse(pugi::xml_node verseNode)
{
    assert(m_currentNote);

    Verse *verse = new Verse();
    verse->SetN(1);
    Syl *syl = new Syl();
    Text *text = new Text();
    std::string sylText = this->ChildAsString(verseNode, "Syllable");
    text->SetText(UTF8to32(sylText));

    if (verseNode.child("WordEnd")) {
        syl->SetWordpos(sylLog_WORDPOS_t);
        m_isInSyllable = false;
    }
    else {
        if (m_isInSyllable) {
            syl->SetWordpos(sylLog_WORDPOS_m);
        }
        else {
            syl->SetWordpos(sylLog_WORDPOS_i);
        }
        m_isInSyllable = true;
        syl->SetCon(sylLog_CON_d);
    }

    syl->AddChild(text);
    verse->AddChild(syl);
    m_currentNote->AddChild(verse);
}

data_DURATION CmmeInput::ReadDuration(pugi::xml_node durationNode, int &num, int &numbase) const
{
    static const std::map<std::string, data_DURATION> durationMap{
        { "Maxima", DURATION_maxima }, //
        { "Longa", DURATION_longa }, //
        { "Brevis", DURATION_brevis }, //
        { "Semibrevis", DURATION_semibrevis }, //
        { "Minima", DURATION_minima }, //
        { "Semiminima", DURATION_semiminima }, //
        { "Fusa", DURATION_fusa }, //
        { "Semifusa", DURATION_semifusa } //
    };

    assert(m_mensInfo);

    std::string type = this->ChildAsString(durationNode, "Type");
    // Default duration to brevis
    data_DURATION duration = durationMap.contains(type) ? durationMap.at(type) : DURATION_brevis;

    num = VRV_UNSET;
    numbase = VRV_UNSET;

    if (durationNode.child("Length")) {
        num = this->ChildAsInt(durationNode.child("Length"), "Num");
        numbase = this->ChildAsInt(durationNode.child("Length"), "Den");

        std::pair<int, int> ratio = { 1, 1 };

        if (type == "Maxima") {
            ratio.first *= m_mensInfo->modusmaior * m_mensInfo->modusminor * m_mensInfo->tempus * m_mensInfo->prolatio;
        }
        else if (type == "Longa") {
            ratio.first *= m_mensInfo->modusminor * m_mensInfo->tempus * m_mensInfo->prolatio;
        }
        else if (type == "Brevis") {
            ratio.first *= m_mensInfo->tempus * m_mensInfo->prolatio;
        }
        else if (type == "Semibrevis") {
            ratio.first *= m_mensInfo->prolatio;
        }
        else if (type == "Semiminima") {
            ratio.second = 2;
        }
        else if (type == "Fusa") {
            ratio.second = 4;
        }
        else if (type == "Semifusa") {
            ratio.second = 8;
        }

        if (ratio.first != num || ratio.second != numbase) {
            num *= ratio.second;
            numbase *= ratio.first;
        }
        else {
            num = VRV_UNSET;
            numbase = VRV_UNSET;
        }
    }

    return duration;
}

bool CmmeInput::IsClef(const pugi::xml_node clefNode) const
{
    static std::vector<std::string> clefs = { "C", "F", "Fsqr", "Frnd", "G" };

    // Checking this is not enough since it is somethimes missing in CMME files
    if (clefNode.select_node("./Signature")) return false;

    // Also check the clef appearance
    std::string appearance = this->ChildAsString(clefNode, "Appearance");
    return (std::find(clefs.begin(), clefs.end(), appearance) != clefs.end());
}

std::string CmmeInput::AsString(const pugi::xml_node node) const
{
    if (!node) return "";

    if (node.text()) {
        return std::string(node.text().as_string());
    }
    return "";
}

std::string CmmeInput::ChildAsString(const pugi::xml_node node, const std::string &child) const
{
    if (!node) return "";

    pugi::xpath_node childNode = node.select_node(child.c_str());
    if (childNode.node()) {
        return this->AsString(childNode.node());
    }
    return "";
}

int CmmeInput::AsInt(const pugi::xml_node node) const
{
    if (!node) return VRV_UNSET;

    if (node.text()) {
        return (node.text().as_int());
    }
    return VRV_UNSET;
}

int CmmeInput::ChildAsInt(const pugi::xml_node node, const std::string &child) const
{
    if (!node) return VRV_UNSET;

    pugi::xpath_node childNode = node.select_node(child.c_str());
    if (childNode.node()) {
        return this->AsInt(childNode.node());
    }
    return VRV_UNSET;
}

} // namespace vrv
