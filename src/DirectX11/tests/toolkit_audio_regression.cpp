#include <Audio.h>
#include <limits>

int main()
{
    DirectX::AudioListener listener;
    if (!listener.IsValid())
        return 1;
    listener.Position.x = std::numeric_limits<float>::quiet_NaN();
    return listener.IsValid() ? 1 : 0;
}
