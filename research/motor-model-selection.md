# Motor Model Selection for Medical Contrast/Saline Injectors

A motor for a contrast or saline medical injector is typically a high-precision, low-speed motor requiring precise control over flow rate and pressure. Based on these requirements, a **second-order model** is the most suitable choice.

## Rationale

**Precision control** — A second-order model captures motor dynamics more accurately, including inertia, damping, and stiffness, which is essential for precise flow rate and pressure control.

**Low-speed operation** — Medical injectors typically operate at low speeds (1–10 mL/s), making the acceleration and deceleration phases critical. A second-order model captures these dynamics more accurately, ensuring smooth and precise injection control.

**Pressure and flow rate response** — A second-order model captures the motor's response to changes in pressure and flow rate, which is critical for maintaining precise control over the injection process.

**Non-linear effects** — Medical injectors often exhibit non-linear effects such as friction, backlash, or hysteresis. A second-order model captures these effects more accurately, enabling better control and compensation.

## Relevant Motor Characteristics

- Low-speed operation (1–10 mL/s)
- High-precision control over flow rate and pressure
- Non-linear effects (friction, backlash)
- Small motor size and low inertia
- High-torque density and low-voltage operation

## Common Motor Types

| Motor Type | Notes |
|---|---|
| Stepper motors | Well-suited for low-speed, high-precision applications; accurately modeled with a second-order model |
| Brushless DC motors | Also well-suited for low-speed, high-precision applications; accurately modeled with a second-order model |
| Linear motors | Often used in medical injectors; accurately modeled with a second-order model |

## Conclusion

A second-order model provides a good balance between accuracy and complexity for modeling a motor in a contrast or saline medical injector.
