#pragma once

class SynchronousValueSource : public Value::ValueSource {
public:
    SynchronousValueSource()
    {
    }

    explicit SynchronousValueSource(var const& initialValue)
        : value(initialValue)
    {
    }

    var getValue() const override
    {
        return value;
    }

    void setValue(var const& newValue) override
    {
        if (!newValue.equalsWithSameType(value)) {
            value = newValue;
            sendChangeMessage(true);
        }
    }

private:
    var value;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynchronousValueSource)
};

#define SynchronousValue(x) Value(new SynchronousValueSource(x));
