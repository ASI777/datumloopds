#pragma once

#include <QObject>

namespace datumloop {

class DatumEditorType final {
    Q_GADGET
public:
    enum Value {
        Viewport = 0,
        Outliner = 1,
        Properties = 2
    };
    Q_ENUM(Value)
};

}  // namespace datumloop
