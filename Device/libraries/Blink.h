// Led setup
#define LED_BUILTIN 4

void Blink(int num, int on, int off) {
for (int i=0;i<num;i++)
  {  
    digitalWrite(LED_BUILTIN, HIGH);
    delay(on);
    digitalWrite(LED_BUILTIN, LOW);
    delay(off);
  }
}
void Blink(bool on)
{
  if (on)
    digitalWrite(LED_BUILTIN, HIGH);
  else
    digitalWrite(LED_BUILTIN, LOW);
}
void BlinkInit()
{
  pinMode (LED_BUILTIN, OUTPUT);
}