#include "PresetManager.h"
#include "FactoryPresets.h"
#include "../Parameters/ParameterManager.h"

namespace dsq
{
static const char* const kExtension = ".dscountpreset"; // same as 1.x; files carry a version

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

juce::Array<juce::RangedAudioParameter*> PresetManager::soundParameters() const
{
    juce::Array<juce::RangedAudioParameter*> list;
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! ParameterManager::isSessionParameter (r->getParameterID()))
                list.add (r);
    return list;
}

void PresetManager::setPlain (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

juce::File PresetManager::userFolder() const
{
#if JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets");
#else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif
    return base.getChildFile ("Yishay Raziel/DS-COUNT");
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (auto& p : factoryPresets())
        names.add (p.name);
    return names;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray names;
    for (auto& f : userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void PresetManager::applyDefaults()
{
    for (auto* p : soundParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::loadInit()
{
    applyDefaults();
    current = "Init";
    isUser = false;
}

void PresetManager::loadFactory (int index)
{
    const auto& all = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) all.size()))
        return;
    applyDefaults();
    for (auto& v : all[(size_t) index].values)
        if (! ParameterManager::isSessionParameter (v.id))
            setPlain (v.id, v.value);
    current = all[(size_t) index].name;
    isUser = false;
}

juce::ValueTree PresetManager::soundSnapshot() const
{
    juce::ValueTree t ("DSCOUNTPRESET");
    t.setProperty ("version", 2, nullptr);
    for (auto* p : soundParameters())
    {
        juce::ValueTree v ("P");
        v.setProperty ("id", p->getParameterID(), nullptr);
        v.setProperty ("value", (double) p->convertFrom0to1 (p->getValue()), nullptr);
        t.appendChild (v, nullptr);
    }
    return t;
}

void PresetManager::restoreSound (const juce::ValueTree& t)
{
    applyDefaults();
    const bool v1 = (int) t.getProperty ("version", 1) < 2;
    for (auto v : t)
    {
        auto id = v.getProperty ("id").toString();
        if (v1)
            id = ParameterManager::migrateV1Id (id);
        if (id.isNotEmpty() && ! ParameterManager::isSessionParameter (id))
            setPlain (id, (float) (double) v.getProperty ("value"));
    }
}

bool PresetManager::saveUser (const juce::String& nameIn)
{
    const auto name = juce::File::createLegalFileName (nameIn.trim());
    if (name.isEmpty())
        return false;
    userFolder().createDirectory();
    auto xml = soundSnapshot().createXml();
    if (xml == nullptr || ! xml->writeTo (userFolder().getChildFile (name + kExtension)))
        return false;
    current = name;
    isUser = true;
    return true;
}

bool PresetManager::loadUser (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userFolder().getChildFile (name + kExtension));
    if (xml == nullptr || ! xml->hasTagName ("DSCOUNTPRESET"))
        return false;
    restoreSound (juce::ValueTree::fromXml (*xml));
    current = name;
    isUser = true;
    return true;
}

bool PresetManager::renameUser (const juce::String& oldName, const juce::String& newNameIn)
{
    const auto newName = juce::File::createLegalFileName (newNameIn.trim());
    auto from = userFolder().getChildFile (oldName + kExtension);
    auto to = userFolder().getChildFile (newName + kExtension);
    if (newName.isEmpty() || ! from.existsAsFile() || to.exists() || ! from.moveFileTo (to))
        return false;
    if (isUser && current == oldName)
        current = newName;
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    if (! userFolder().getChildFile (name + kExtension).deleteFile())
        return false;
    if (isUser && current == name)
    {
        current = "Init";
        isUser = false;
    }
    return true;
}

void PresetManager::step (int direction)
{
    const auto factory = factoryNames();
    const auto user = userNames();
    const int total = factory.size() + user.size();
    if (total == 0)
        return;
    int index = isUser ? factory.size() + user.indexOf (current) : factory.indexOf (current);
    index = index < 0 ? (direction > 0 ? 0 : total - 1) : (index + direction + total) % total;
    if (index < factory.size())
        loadFactory (index);
    else
        loadUser (user[index - factory.size()]);
}

void PresetManager::selectSlot (int newSlot)
{
    if (newSlot == slot)
        return;
    auto here = soundSnapshot();
    const auto hereName = current;
    const bool hereUser = isUser;
    if (other.isValid())
    {
        restoreSound (other);
        current = otherName;
        isUser = otherIsUser;
    }
    other = here;
    otherName = hereName;
    otherIsUser = hereUser;
    slot = newSlot;
}

void PresetManager::copyActiveToOther()
{
    other = soundSnapshot();
    otherName = current;
    otherIsUser = isUser;
}

void PresetManager::swapSlots()
{
    if (! other.isValid())
        copyActiveToOther();
    auto here = soundSnapshot();
    restoreSound (other);
    std::swap (current, otherName);
    std::swap (isUser, otherIsUser);
    other = here;
}

void PresetManager::copyChannel (const char* modulePrefix, int from, int to)
{
    const juce::String src = juce::String (modulePrefix) + "_ch" + juce::String (from + 1) + "_";
    const juce::String dst = juce::String (modulePrefix) + "_ch" + juce::String (to + 1) + "_";
    for (auto* p : soundParameters())
        if (p->getParameterID().startsWith (src))
            if (auto* d = apvts.getParameter (dst + p->getParameterID().substring (src.length())))
                d->setValueNotifyingHost (p->getValue());
}

void PresetManager::writeTo (juce::ValueTree& parent) const
{
    parent.setProperty ("presetName", current, nullptr);
    parent.setProperty ("presetIsUser", isUser, nullptr);
    parent.setProperty ("abSlot", slot, nullptr);
    parent.setProperty ("otherName", otherName, nullptr);
    parent.setProperty ("otherIsUser", otherIsUser, nullptr);
    if (other.isValid())
    {
        juce::ValueTree wrapper ("OTHERSLOT");
        wrapper.appendChild (other.createCopy(), nullptr);
        parent.appendChild (wrapper, nullptr);
    }
}

void PresetManager::readFrom (const juce::ValueTree& parent)
{
    current = parent.getProperty ("presetName", "Init").toString();
    isUser = (bool) parent.getProperty ("presetIsUser", false);
    slot = (int) parent.getProperty ("abSlot", 0);
    otherName = parent.getProperty ("otherName", "Init").toString();
    otherIsUser = (bool) parent.getProperty ("otherIsUser", false);
    auto wrapper = parent.getChildWithName ("OTHERSLOT");
    other = wrapper.isValid() && wrapper.getNumChildren() > 0 ? wrapper.getChild (0).createCopy() : juce::ValueTree();
}
} // namespace dsq
