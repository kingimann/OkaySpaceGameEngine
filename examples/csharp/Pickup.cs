// C# example: a pickup that bobs up and down, spins, and is collected when the Player gets close.
// Public fields show in the Inspector and are kept by a saved scene or save game.
// Needs the .NET 8 SDK installed and an engine built with OKAY_WITH_CSHARP=ON (Windows).
using OkayScripting;

public class Pickup : Behaviour
{
    public float speed = 90f;          // spin, in degrees per second
    public float height = 0.5f;        // how far it bobs up and down
    public float pickDistance = 1.5f;  // how close the Player must be
    float age;
    bool collected;
    Vector3 origin;

    void Start()
    {
        origin = transform.position;
        Debug.Log("Pickup ready at " + origin);
    }

    void Update()
    {
        age += Time.deltaTime;
        transform.position = origin + new Vector3(0f, Mathf.Sin(age * 2f) * height, 0f);
        transform.Rotate(new Vector3(0f, speed * Time.deltaTime, 0f));

        GameObject player = Find("Player");
        if (player != null && !collected)
        {
            float d = Vector3.Distance(player.transform.position, origin);
            if (d < pickDistance)
            {
                collected = true;
                Debug.Log("Picked up!");
                Destroy(gameObject, 0.1f);
            }
        }
    }
}
