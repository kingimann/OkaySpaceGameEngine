using UnityEngine;
using System.Collections;

namespace Playground
{
    // Move with W A S D (or the arrow keys). Space hops; every hop is counted.
    public class PlayerMover : MonoBehaviour
    {
        [Header("Movement")]
        [SerializeField] private float speed = 5f;
        [Range(0, 5)] public float hopHeight = 1.5f;

        // A property: other scripts can read it, only this class sets it.
        public int Hops { get; private set; }

        void Start()
        {
            Debug.Log("C# playground: move with W A S D, hop with Space.");
        }

        void Update()
        {
            float h = Input.GetAxis("Horizontal");
            float v = Input.GetAxis("Vertical");
            transform.position += new Vector3(h, 0, -v) * speed * Time.deltaTime;
            if (Input.GetKeyDown(KeyCode.Space)) StartCoroutine(Hop());
        }

        // A coroutine: up, wait a quarter of a second, down.
        IEnumerator Hop()
        {
            Hops++;
            transform.Translate(0, hopHeight, 0);
            yield return new WaitForSeconds(0.25f);
            transform.Translate(0, -hopHeight, 0);
            Debug.Log($"Hops: {Hops}");
        }
    }
}
