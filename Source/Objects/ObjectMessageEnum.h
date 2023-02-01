#pragma once

#include <map>
#include <JuceHeader.h>

enum objectMessage { msg_float
                    ,msg_bang
                    ,msg_list
                    ,msg_set
                    ,msg_symbol
                    ,msg_flashtime
                    ,msg_bgcolor
                    ,msg_fgcolor
                    ,msg_color
                    ,msg_vis
                    ,msg_send
                    ,msg_receive
                    ,msg_min
                    ,msg_max
                    ,msg_coords
                    ,msg_lowc
                    ,msg_oct
                    ,msg_8ves
                    ,msg_open
                    ,msg_orientation
                    ,msg_number
                    ,msg_lin
                    ,msg_log
                    ,msg_range
                    ,msg_steady
                    ,msg_nonzero
                    ,msg_label
                    ,msg_label_pos
                    ,msg_label_font
                    ,msg_vis_size
                    ,msg_init
};

static inline std::map<String, objectMessage> objectMessageMapped = {
    { "float"       ,msg_float       },
    { "bang"        ,msg_bang        },
    { "list"        ,msg_list        },
    { "set"         ,msg_set         },
    { "symbol"      ,msg_symbol      },
    { "flashtime"   ,msg_flashtime   },
    { "bgcolor"     ,msg_bgcolor     },
    { "fgcolor"     ,msg_fgcolor     },
    { "color"       ,msg_color       },
    { "vis"         ,msg_vis         },
    { "send"        ,msg_send        },
    { "receive"     ,msg_receive     },
    { "min"         ,msg_min         },
    { "max"         ,msg_max         },
    { "coords"      ,msg_coords      },
    { "lowc"        ,msg_lowc        },
    { "oct"         ,msg_oct         },
    { "8ves"        ,msg_8ves        },
    { "open"        ,msg_open        },
    { "orientation" ,msg_orientation },
    { "number"      ,msg_number      },
    { "lin"         ,msg_lin         },
    { "log"         ,msg_log         },
    { "range"       ,msg_range       },
    { "steady"      ,msg_steady      },
    { "nonzero"     ,msg_nonzero     },
    { "label"       ,msg_label       },
    { "label_pos"   ,msg_label_pos   },
    { "label_font"  ,msg_label_font  },
    { "vis_size"    ,msg_vis_size    },
    { "init"        ,msg_init        }
};
