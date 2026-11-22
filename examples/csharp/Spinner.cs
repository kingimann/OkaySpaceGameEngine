using UnityEngine;

// Turns the object around its up axis. Change Degrees Per Second in the Inspector.
public class Spinner : MonoBehaviour
{
    public float degreesPerSecond = 90f;
    private float turned;

    // A property with code: worked out each time it is read.
    public float Turns => turned / 360f;

    void Update()
    {
        float step = degreesPerSecond * Time.deltaTime;
        transform.Rotate(0, step, 0);
        turned += step;
    }
}
