/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

class FunctionObject final : public ObjectBase {

    struct t_fake_function {
        t_object x_obj;
        t_glist* x_glist;
        t_edit_proxy* x_proxy;
        int x_state;
        int x_n_states;
        int x_flag;
        int x_s_flag;
        int x_r_flag;
        int x_sel;
        int x_width;
        int x_height;
        int x_init;
        int x_grabbed; // number of grabbed point, for moving it/deleting it
        int x_shift;
        int x_snd_set;
        int x_rcv_set;
        int x_zoom;
        int x_edit;
        t_symbol* x_send;
        t_symbol* x_receive;
        t_symbol* x_snd_raw;
        t_symbol* x_rcv_raw;
        float* x_points;
        float* x_dur;
        float x_total_duration;
        float x_min;
        float x_max;
        float x_min_point;
        float x_max_point;
        float x_pointer_x;
        float x_pointer_y;
        unsigned char x_fgcolor[3];
        unsigned char x_bgcolor[3];
    };

    int hoverIdx = -1;
    int dragIdx = -1;
    bool newPointAdded = false;

    Value range;
    Value primaryColour;
    Value secondaryColour;
    Value sendSymbol;
    Value receiveSymbol;

public:
    FunctionObject(void* ptr, Object* object)
        : ObjectBase(ptr, object)
    {
        auto* function = static_cast<t_fake_function*>(ptr);
        secondaryColour = colourFromHexArray(function->x_bgcolor).toString();
        primaryColour = colourFromHexArray(function->x_fgcolor).toString();

        Array<var> arr = { function->x_min, function->x_max };
        range = var(arr);

        auto sndSym = String::fromUTF8(function->x_send->s_name);
        auto rcvSym = String::fromUTF8(function->x_receive->s_name);

        sendSymbol = sndSym != "empty" ? sndSym : "";
        receiveSymbol = rcvSym != "empty" ? rcvSym : "";
    }

    // std::pair<float, float> range;
    Array<Point<float>> points;

    void setPdBounds(Rectangle<int> b) override
    {
        libpd_moveobj(cnv->patch.getPointer(), static_cast<t_gobj*>(ptr), b.getX(), b.getY());

        auto* function = static_cast<t_fake_function*>(ptr);
        function->x_width = b.getWidth();
        function->x_height = b.getHeight();
    }

    Rectangle<int> getPdBounds() override
    {
        pd->lockAudioThread();

        int x = 0, y = 0, w = 0, h = 0;
        libpd_get_object_bounds(cnv->patch.getPointer(), ptr, &x, &y, &w, &h);

        pd->unlockAudioThread();

        return { x, y, w, h };
    }

    void resized() override
    {
        static_cast<t_fake_function*>(ptr)->x_width = getWidth();
        static_cast<t_fake_function*>(ptr)->x_height = getHeight();
    }

    Array<Point<float>> getRealPoints()
    {
        auto realPoints = Array<Point<float>>();
        for (auto point : points) {
            point.x = jmap<float>(point.x, 0.0f, 1.0f, 3, getWidth() - 3);
            point.y = jmap<float>(point.y, 0.0f, 1.0f, getHeight() - 3, 3);
            realPoints.add(point);
        }

        return realPoints;
    }

    void setRealPoints(Array<Point<float>> const& points)
    {
        auto* function = static_cast<t_fake_function*>(ptr);
        for (int i = 0; i < points.size(); i++) {
            function->x_points[i] = jmap<float>(points[i].y, getHeight() - 3, 3, 1.0f, 0.0f);
            function->x_dur[i] = jmap<float>(points[i].x, 3, getWidth() - 3, 0.0f, 1.0f);
        }
    }

    void paint(Graphics& g) override
    {
        g.setColour(Colour::fromString(secondaryColour.toString()));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), PlugDataLook::objectCornerRadius);

        bool selected = cnv->isSelected(object) && !cnv->isGraph;
        bool editing = cnv->locked == var(true) || cnv->presentationMode == var(true) || ModifierKeys::getCurrentModifiers().isCtrlDown();
        auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

        g.setColour(outlineColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), PlugDataLook::objectCornerRadius, 1.0f);

        g.setColour(Colour::fromString(primaryColour.toString()));

        auto realPoints = getRealPoints();
        auto lastPoint = realPoints[0];
        for (int i = 1; i < realPoints.size(); i++) {
            auto newPoint = realPoints[i];
            g.drawLine({ lastPoint, newPoint });
            lastPoint = newPoint;
        }

        for (int i = 0; i < realPoints.size(); i++) {
            auto point = realPoints[i];
            // Make sure line isn't visible through the hole
            g.setColour(Colour::fromString(secondaryColour.toString()));
            g.fillEllipse(Rectangle<float>().withCentre(point).withSizeKeepingCentre(5, 5));

            g.setColour(Colour::fromString(hoverIdx == i && editing ? outlineColour.toString() : primaryColour.toString()));
            g.drawEllipse(Rectangle<float>().withCentre(point).withSizeKeepingCentre(5, 5), 1.5f);
        }
    }

    void getPointsFromFunction()
    {
        // Don't update while dragging
        if (dragIdx != -1)
            return;

        points.clear();
        auto* function = static_cast<t_fake_function*>(ptr);

        setRange({ function->x_min, function->x_max });

        for (int i = 0; i < function->x_n_states + 1; i++) {
            auto x = function->x_dur[i] / function->x_dur[function->x_n_states];
            auto y = jmap(function->x_points[i], function->x_min, function->x_max, 0.0f, 1.0f);
            points.add({ x, y });
        }

        repaint();
    };

    static int compareElements(Point<float> a, Point<float> b)
    {
        if (a.x < b.x)
            return -1;
        else if (a.x > b.x)
            return 1;
        else
            return 0;
    }

    void setHoverIdx(int i)
    {
        hoverIdx = i;
        repaint();
    }

    void resetHoverIdx()
    {
        setHoverIdx(-1);
    }

    bool hitTest(int x, int y) override
    {
        resetHoverIdx();
        auto realPoints = getRealPoints();
        for (int i = 0; i < realPoints.size(); i++) {
            auto hoverBounds = Rectangle<float>().withCentre(realPoints[i]).withSizeKeepingCentre(7, 7);
            if (hoverBounds.contains(x, y)) {
                setHoverIdx(i);
            }
        }
        return ObjectBase::hitTest(x, y);
    }

    void mouseExit(MouseEvent const& e) override
    {
        resetHoverIdx();
    }

    void mouseDown(MouseEvent const& e) override
    {
        if (ModifierKeys::getCurrentModifiers().isRightButtonDown())
            return;

        auto realPoints = getRealPoints();
        for (int i = 0; i < realPoints.size(); i++) {
            auto clickBounds = Rectangle<float>().withCentre(realPoints[i]).withSizeKeepingCentre(7, 7);
            if (clickBounds.contains(e.x, e.y)) {
                dragIdx = i;
                if (e.getNumberOfClicks() == 2) {
                    dragIdx = -1;
                    if (i == 0 || i == realPoints.size() - 1) {
                        points.getReference(i).y = 0.0f;
                        resetHoverIdx();
                        triggerOutput();
                        return;
                    }
                    points.remove(i);
                    resetHoverIdx();
                    triggerOutput();
                    return;
                }
                return;
            }
        }

        float newX = jmap(static_cast<float>(e.x), 3.0f, getWidth() - 3.0f, 0.0f, 1.0f);
        float newY = jmap(static_cast<float>(e.y), 3.0f, getHeight() - 3.0f, 1.0f, 0.0f);

        dragIdx = points.addSorted(*this, { newX, newY });

        triggerOutput();
    }

    std::pair<float, float> getRange()
    {
        auto& arr = *range.getValue().getArray();
        return { static_cast<float>(arr[0]), static_cast<float>(arr[1]) };
    }
    void setRange(std::pair<float, float> newRange)
    {
        auto& arr = *range.getValue().getArray();
        arr[0] = newRange.first;
        arr[1] = newRange.second;

        auto* function = static_cast<t_fake_function*>(ptr);
        function->x_min = newRange.first;
        function->x_max = newRange.second;
    }

    void mouseDrag(MouseEvent const& e) override
    {
        auto [min, max] = getRange();
        bool changed = false;

        // For first and last point, only adjust y position
        if (dragIdx == 0 || dragIdx == points.size() - 1) {
            float newY = jlimit(min, max, jmap(static_cast<float>(e.y), 3.0f, getHeight() - 3.0f, 1.0f, 0.0f));
            if (newY != points.getReference(dragIdx).y) {
                points.getReference(dragIdx).y = newY;
                changed = true;
            }

        }

        else if (dragIdx > 0) {
            float minX = points[dragIdx - 1].x;
            float maxX = points[dragIdx + 1].x;

            float newX = jlimit(minX, maxX, jmap(static_cast<float>(e.x), 3.0f, getWidth() - 3.0f, 0.0f, 1.0f));

            float newY = jlimit(min, max, jmap(static_cast<float>(e.y), 3.0f, getHeight() - 3.0f, 1.0f, 0.0f));

            auto newPoint = Point<float>(newX, newY);
            if (points[dragIdx] != newPoint) {
                points.set(dragIdx, newPoint);
                changed = true;
            }
        }

        repaint();
        if (changed)
            triggerOutput();
    }

    void mouseUp(MouseEvent const& e) override
    {
        auto* function = static_cast<t_fake_function*>(ptr);
        points.sort(*this);

        auto scale = function->x_dur[function->x_n_states];

        for (int i = 0; i < points.size(); i++) {
            function->x_points[i] = jmap(points[i].y, 0.0f, 1.0f, function->x_min, function->x_max);
            function->x_dur[i] = points[i].x * scale;
        }

        function->x_n_states = points.size() - 1;

        dragIdx = -1;

        getPointsFromFunction();
    }

    void triggerOutput()
    {

        auto* x = static_cast<t_fake_function*>(ptr);
        int ac = points.size() * 2 + 1;

        auto scale = x->x_dur[x->x_n_states];

        auto at = std::vector<t_atom>(ac);
        float firstPoint = jmap<float>(points[0].y, 0.0f, 1.0f, x->x_min, x->x_max);
        SETFLOAT(at.data(), firstPoint); // get 1st

        x->x_state = 0;
        for (int i = 1; i < ac; i++) { // get the rest

            float dur = jmap<float>(points[x->x_state + 1].x - points[x->x_state].x, 0.0f, 1.0f, 0.0f, scale);

            SETFLOAT(at.data() + i, dur); // duration
            i++, x->x_state++;
            float point = jmap<float>(points[x->x_state].y, 0.0f, 1.0f, x->x_min, x->x_max);
            if (point < x->x_min_point)
                x->x_min_point = point;
            if (point > x->x_max_point)
                x->x_max_point = point;
            SETFLOAT(at.data() + i, point);
        }

        pd->enqueueFunction([_this = SafePointer(this), x, at, ac]() mutable {
            if (!_this || _this->cnv->patch.objectWasDeleted(x))
                return;

            outlet_list(x->x_obj.ob_outlet, &s_list, ac - 2, at.data());
            if (x->x_send != &s_ && x->x_send->s_thing)
                pd_list(x->x_send->s_thing, &s_list, ac - 2, at.data());
        });
    }

    void valueChanged(Value& v) override
    {
        auto* function = static_cast<t_fake_function*>(ptr);
        if (v.refersToSameSourceAs(primaryColour)) {
            colourToHexArray(Colour::fromString(primaryColour.toString()), function->x_fgcolor);
            repaint();
        } else if (v.refersToSameSourceAs(secondaryColour)) {
            colourToHexArray(Colour::fromString(secondaryColour.toString()), function->x_bgcolor);
            repaint();
        } else if (v.refersToSameSourceAs(sendSymbol)) {
            auto symbol = sendSymbol.toString();
            t_atom atom;
            SETSYMBOL(&atom, pd->generateSymbol(symbol));
            pd_typedmess((t_pd*)function, pd->generateSymbol("send"), 1, &atom);
        } else if (v.refersToSameSourceAs(receiveSymbol)) {

            auto symbol = receiveSymbol.toString();
            t_atom atom;
            SETSYMBOL(&atom, pd->generateSymbol(symbol));
            pd_typedmess((t_pd*)function, pd->generateSymbol("receive"), 1, &atom);

        } else if (v.refersToSameSourceAs(range)) {
            setRange(getRange());
            getPointsFromFunction();
        }
    }

    ObjectParameters getParameters() override
    {
        return {
            { "Foreground", tColour, cAppearance, &primaryColour, {} },
            { "Background", tColour, cAppearance, &secondaryColour, {} },
            { "Range", tRange, cGeneral, &range, {} },
            { "Receive Symbol", tString, cGeneral, &receiveSymbol, {} },
            { "Send Symbol", tString, cGeneral, &sendSymbol, {} },
        };
    }

    Colour colourFromHexArray(unsigned char* hex)
    {
        return Colour(hex[0], hex[1], hex[2]);
    }
    void colourToHexArray(Colour colour, unsigned char* hex)
    {
        hex[0] = colour.getRed();
        hex[1] = colour.getGreen();
        hex[2] = colour.getBlue();
    }

    void receiveObjectMessage(String const& symbol, std::vector<pd::Atom>& atoms) override
    {
        switch (hash(symbol)) {
        case hash("send"): {
            if (atoms.size() >= 1)
                setParameterExcludingListener(sendSymbol, atoms[0].getSymbol());
            break;
        }
        case hash("receive"): {
            if (atoms.size() >= 1)
                setParameterExcludingListener(receiveSymbol, atoms[0].getSymbol());
            break;
        }
        case hash("list"): {
            getPointsFromFunction();
            break;
        }
        case hash("min"):
        case hash("max"): {
            auto* function = static_cast<t_fake_function*>(ptr);
            Array<var> arr = { function->x_min, function->x_max };
            setParameterExcludingListener(range, var(arr));
            getPointsFromFunction();
            break;
        }
        default:
            break;
        }
    }
};
