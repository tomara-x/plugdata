/*
 // Copyright (c) 2021-2022 Timothy Schoen and Pierre Guillot
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "ObjectBase.h"

extern "C" {

void canvas_setgraph(t_glist* x, int flag, int nogoprect);

#include <m_pd.h>
#include <g_canvas.h>
#include <m_imp.h>
#include <g_all_guis.h>
#include <g_undo.h>
}

#include "Object.h"
#include "Canvas.h"
#include "Tabbar.h"
#include "SuggestionComponent.h"
#include "PluginEditor.h"
#include "LookAndFeel.h"
#include "Pd/PdPatch.h"
#include "Utility/ObjectBoundsConstrainer.h"

#include "IEMHelper.h"
#include "AtomHelper.h"

#include "TextObject.h"
#include "ToggleObject.h"
#include "MessageObject.h"
#include "MouseObject.h"
#include "BangObject.h"
#include "ButtonObject.h"
#include "RadioObject.h"
#include "SliderObject.h"
#include "ArrayObject.h"
#include "GraphOnParent.h"
#include "KeyboardObject.h"
#include "KeyObject.h"
#include "MessboxObject.h"
#include "MousePadObject.h"
#include "NumberObject.h"
#include "NumboxTildeObject.h"
#include "CanvasObject.h"
#include "PictureObject.h"
#include "VUMeterObject.h"
#include "ListObject.h"
#include "SubpatchObject.h"
#include "CloneObject.h"
#include "CommentObject.h"
#include "CycloneCommentObject.h"
#include "FloatAtomObject.h"
#include "SymbolAtomObject.h"
#include "ScalarObject.h"
#include "TextDefineObject.h"
#include "CanvasListenerObjects.h"
#include "ScopeObject.h"
#include "FunctionObject.h"
#include "BicoeffObject.h"

// Class for non-patchable objects
class NonPatchable : public ObjectBase {

public:
    NonPatchable(void* obj, Object* parent)
        : ObjectBase(obj, parent)
    {
        parent->setVisible(false);
    }

    Rectangle<int> getPdBounds() override { return {0, 0, 1, 1}; };
    void setPdBounds(Rectangle<int> newBounds) override {};
};

void ObjectLabel::ObjectListener::componentMovedOrResized(Component& component, bool moved, bool resized)
{

    dynamic_cast<Object&>(component).gui->updateLabel();
}

ObjectBase::ObjectBase(void* obj, Object* parent)
    : ptr(obj)
    , object(parent)
    , cnv(parent->cnv)
    , pd(parent->cnv->pd)
{
    pd->registerMessageListener(ptr, this);

    updateLabel(); // TODO: fix virtual call from constructor

    setWantsKeyboardFocus(true);

    setLookAndFeel(new PlugDataLook());

    MessageManager::callAsync([_this = SafePointer(this)] {
        if (_this) {
            _this->initialiseParameters();
        }
    });
}

ObjectBase::~ObjectBase()
{
    pd->unregisterMessageListener(ptr, this);

    auto* lnf = &getLookAndFeel();
    setLookAndFeel(nullptr);
    delete lnf;
}

String ObjectBase::getText()
{
    if (!cnv->patch.checkObject(ptr))
        return "";

    cnv->pd->setThis();

    char* text = nullptr;
    int size = 0;
    libpd_get_object_text(ptr, &text, &size);

    if (text && size) {

        auto txt = String::fromUTF8(text, size);
        freebytes(static_cast<void*>(text), static_cast<size_t>(size) * sizeof(char));
        return txt;
    }

    return "";
}

String ObjectBase::getType() const
{
    // TODO: callback lock can cause deadlock :(
    // We have a lot of threading problems to fix...

    // ScopedLock lock(*pd->getCallbackLock());

    if (ptr) {
        // Check if it's an abstraction or subpatch
        if (pd_class(static_cast<t_pd*>(ptr)) == canvas_class && canvas_isabstraction((t_canvas*)ptr)) {
            char namebuf[MAXPDSTRING];
            t_object* ob = (t_object*)ptr;
            int ac = binbuf_getnatom(ob->te_binbuf);
            t_atom* av = binbuf_getvec(ob->te_binbuf);
            if (ac < 1)
                return String();
            atom_string(av, namebuf, MAXPDSTRING);

            return String::fromUTF8(namebuf).fromLastOccurrenceOf("/", false, false);
        }
        // Deal with different text objects
        switch (hash(libpd_get_object_class_name(ptr))) {
        case hash("text"):
            if (static_cast<t_text*>(ptr)->te_type == T_OBJECT)
                return String("invalid");
            if (static_cast<t_text*>(ptr)->te_type == T_TEXT)
                return String("comment");
            if (static_cast<t_text*>(ptr)->te_type == T_MESSAGE)
                return String("message");
            break;
        // Deal with atoms
        case hash("gatom"):
            if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_FLOAT)
                return "floatbox";
            if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_SYMBOL)
                return "symbolbox";
            if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_NULL)
                return "listbox";
            break;
        default:
            break;
        }
        // Get class name for all other objects
        if (auto* name = libpd_get_object_class_name(ptr)) {
            return String::fromUTF8(name);
        }
    }

    sys_unlock();

    return {};
}

// Called in destructor of subpatch and graph class
// Makes sure that any tabs refering to the now deleted patch will be closed
void ObjectBase::closeOpenedSubpatchers()
{
    auto* editor = object->cnv->editor;

    for (auto* canvas : editor->canvases) {
        if (canvas && canvas->patch == *getPatch()) {

            canvas->editor->closeTab(canvas);
            break;
        }
    }
}


bool ObjectBase::click()
{
    pd->setThis();

    auto* pdObj = static_cast<t_gobj*>(ptr)->g_pd;
    
    // Check if click method exists, if so, call it
    t_methodentry* mlist;
    
    auto* pdinstance = pd_this;
#if PDINSTANCE
        mlist = pdObj->c_methods[pdinstance->pd_instanceno];
#else
        mlist = pdObj->c_methods;
#endif
    
    for(int i = 0; i < pdObj->c_nmethod; i++)
    {
        auto& method = mlist[i];
        if(mlist[i].me_name == gensym("click") && mlist[i].me_arg[0] == '\0') {
            pd->enqueueDirectMessages(ptr, "click", {});
            return true;
        }
    }
    
    return false;
}

void ObjectBase::openSubpatch()
{
    auto* subpatch = getPatch();

    if (!subpatch)
        return;

    auto* glist = subpatch->getPointer();

    if (!glist)
        return;

    auto abstraction = canvas_isabstraction(glist);
    File path;

    if (abstraction) {
        path = File(String::fromUTF8(canvas_getdir(subpatch->getPointer())->s_name)).getChildFile(String::fromUTF8(glist->gl_name->s_name)).withFileExtension("pd");
    }

    // Check if subpatch is already opened
    for (auto* cnv : cnv->editor->canvases) {
        if (cnv->patch == *subpatch) {
            auto* tabbar = cnv->getTabbar();
            tabbar->setCurrentTabIndex(cnv->getTabIndex());
            return;
        }
    }

    auto* newPatch = cnv->editor->pd->patches.add(subpatch);
    auto* newCanvas = cnv->editor->canvases.add(new Canvas(cnv->editor, *newPatch, nullptr));

    newPatch->setCurrentFile(path);

    cnv->editor->addTab(newCanvas);
    newCanvas->checkBounds();
}

void ObjectBase::moveToFront()
{
    pd->setThis();
    libpd_tofront(cnv->patch.getPointer(), static_cast<t_gobj*>(ptr));
}

void ObjectBase::moveToBack()
{
    pd->setThis();
    libpd_toback(cnv->patch.getPointer(), static_cast<t_gobj*>(ptr));
}

void ObjectBase::paint(Graphics& g)
{
    g.setColour(object->findColour(PlugDataColour::guiObjectBackgroundColourId));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius);

    bool selected = cnv->isSelected(object) && !cnv->isGraph;
    auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

    g.setColour(outlineColour);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius, 1.0f);
}

void ObjectBase::initialiseParameters()
{
    getLookAndFeel().setColour(Label::textWhenEditingColourId, object->findColour(Label::textWhenEditingColourId));
    getLookAndFeel().setColour(Label::textColourId, object->findColour(Label::textColourId));

    auto params = getParameters();
    for (auto& [name, type, cat, value, list] : params) {
        value->addListener(this);

        // Push current parameters to pd
        valueChanged(*value);
    }

    repaint();
}

ObjectParameters ObjectBase::getParameters()
{
    return {};
}

void ObjectBase::startEdition()
{
    if (edited)
        return;

    edited = true;
    pd->enqueueMessages("gui", "mouse", { 1.f });
}

void ObjectBase::stopEdition()
{
    if (!edited)
        return;

    edited = false;
    pd->enqueueMessages("gui", "mouse", { 0.f });
}

void ObjectBase::sendFloatValue(float newValue)
{
    pd->enqueueFunction([newValue, patch = &cnv->patch, ptr = this->ptr](){
        
        if(patch->objectWasDeleted(ptr)) return;
        
        t_atom atom;
        SETFLOAT(&atom, newValue);
        pd_typedmess(static_cast<t_pd*>(ptr), patch->instance->generateSymbol("set"), 1, &atom);
        pd_bang(static_cast<t_pd*>(ptr));
    });

}

ObjectBase* ObjectBase::createGui(void* ptr, Object* parent)
{
    auto const name = hash(libpd_get_object_class_name(ptr));

    // check if object is a patcher object, or something else
    if (!pd_checkobject(static_cast<t_pd*>(ptr)) && name != hash("scalar")) {
        return new NonPatchable(ptr, parent);
    } else {
        switch (name) {
        case hash("bng"):
            return new BangObject(ptr, parent);
        case hash("button"):
            return new ButtonObject(ptr, parent);
        case hash("hsl"):
        case hash("vsl"):
        case hash("slider"):
            return new SliderObject(ptr, parent);
        case hash("tgl"):
            return new ToggleObject(ptr, parent);
        case hash("nbx"):
            return new NumberObject(ptr, parent);
        case hash("numbox~"):
            return new NumboxTildeObject(ptr, parent);
        case hash("vradio"):
        case hash("hradio"):
            return new RadioObject(ptr, parent);
        case hash("cnv"):
            return new CanvasObject(ptr, parent);
        case hash("vu"):
            return new VUMeterObject(ptr, parent);
        case hash("text"): {
            auto* textObj = static_cast<t_text*>(ptr);
            if (textObj->te_type == T_OBJECT) {
                return new TextObject(ptr, parent, false);
            } else {
                return new CommentObject(ptr, parent);
            }
        }
        case hash("comment"):
            return new CycloneCommentObject(ptr, parent);
        // Check if message type text object to prevent confusing it with else/message
        case hash("message"): {
            if (libpd_is_text_object(ptr) && static_cast<t_text*>(ptr)->te_type == T_MESSAGE) {
                return new MessageObject(ptr, parent);
            }
            break;
        }
        case hash("pad"):
            return new MousePadObject(ptr, parent);
        case hash("mouse"):
            return new MouseObject(ptr, parent);
        case hash("keyboard"):
            return new KeyboardObject(ptr, parent);
        case hash("pic"):
            return new PictureObject(ptr, parent);
        case hash("text define"):
            return new TextDefineObject(ptr, parent);
        case hash("gatom"): {
            if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_FLOAT) {
                return new FloatAtomObject(ptr, parent);
            } else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_SYMBOL) {
                return new SymbolAtomObject(ptr, parent);
            } else if (static_cast<t_fake_gatom*>(ptr)->a_flavor == A_NULL) {
                return new ListObject(ptr, parent);
            }
            break;
        }
        case hash("canvas"):
        case hash("graph"): {
            if (static_cast<t_canvas*>(ptr)->gl_list) {
                t_class* c = static_cast<t_canvas*>(ptr)->gl_list->g_pd;
                if (c && c->c_name && (String::fromUTF8(c->c_name->s_name) == "array")) {
                    return new ArrayObject(ptr, parent);
                } else if (static_cast<t_canvas*>(ptr)->gl_isgraph) {
                    return new GraphOnParent(ptr, parent);
                } else { // abstraction or subpatch
                    return new SubpatchObject(ptr, parent);
                }
            } else if (static_cast<t_canvas*>(ptr)->gl_isgraph) {
                return new GraphOnParent(ptr, parent);
            } else {
                return new SubpatchObject(ptr, parent);
            }
        }
        case hash("array define"):
            return new ArrayDefineObject(ptr, parent);
        case hash("clone"):
            return new CloneObject(ptr, parent);
        case hash("pd"):
            return new SubpatchObject(ptr, parent);
        case hash("scalar"): {
            auto* gobj = static_cast<t_gobj*>(ptr);
            if (gobj->g_pd == scalar_class) {
                return new ScalarObject(ptr, parent);
            }
            break;
        }
        case hash("key"):
            return new KeyObject(ptr, parent, KeyObject::Key);
        case hash("keyname"):
            return new KeyObject(ptr, parent, KeyObject::KeyName);
        case hash("keyup"):
            return new KeyObject(ptr, parent, KeyObject::KeyUp);
        // ELSE's [oscope~] and cyclone [scope~] are basically the same object
        case hash("oscope~"):
            return new OscopeObject(ptr, parent);
        case hash("scope~"):
            return new ScopeObject(ptr, parent);
        case hash("function"):
            return new FunctionObject(ptr, parent);
        case hash("bicoeff"):
            return new BicoeffObject(ptr, parent);
        case hash("messbox"):
            return new MessboxObject(ptr, parent);
        case hash("canvas.active"):
            return new CanvasActiveObject(ptr, parent);
        case hash("canvas.mouse"):
            return new CanvasMouseObject(ptr, parent);
        case hash("canvas.vis"):
            return new CanvasVisibleObject(ptr, parent);
        case hash("canvas.zoom"):
            return new CanvasZoomObject(ptr, parent);
        case hash("canvas.edit"):
            return new CanvasEditObject(ptr, parent);
        default:
            break;
        }
    }
    return new TextObject(ptr, parent);
}

bool ObjectBase::canOpenFromMenu()
{
    return zgetfn(static_cast<t_pd*>(ptr), pd->generateSymbol("menu-open")) != nullptr;
}

void ObjectBase::openFromMenu()
{
    pd_typedmess(static_cast<t_pd*>(ptr), pd->generateSymbol("menu-open"), 0, nullptr);
};

bool ObjectBase::hideInGraph()
{
    return false;
}

void ObjectBase::lock(bool isLocked)
{
    setInterceptsMouseClicks(isLocked, isLocked);
}

Canvas* ObjectBase::getCanvas()
{
    return nullptr;
};

pd::Patch* ObjectBase::getPatch()
{
    return nullptr;
};

bool ObjectBase::canReceiveMouseEvent(int x, int y)
{
    return true;
}

void ObjectBase::receiveMessage(String const& symbol, int argc, t_atom* argv)
{
    auto atoms = pd::Atom::fromAtoms(argc, argv);

    MessageManager::callAsync([_this = SafePointer(this), symbol, atoms]() mutable {
        if (!_this || _this->cnv->patch.objectWasDeleted(_this->ptr))
            return;

        switch (hash(symbol)) {
        case hash("size"):
        case hash("delta"):
        case hash("pos"):
        case hash("dim"):
        case hash("width"):
        case hash("height"):
            _this->object->updateBounds();
            break;
        default:
            _this->receiveObjectMessage(symbol, atoms);
        }
    });
}

void ObjectBase::setParameterExcludingListener(Value& parameter, var value)
{
    parameter.removeListener(this);
    parameter.setValue(value);
    parameter.addListener(this);
}

ObjectLabel* ObjectBase::getLabel()
{
    return label.get();
}
bool ObjectBase::isBeingEdited()
{
    return edited;
}
