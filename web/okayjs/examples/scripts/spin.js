// The spinning cube's script: the engine's script API only, so the engine's JavaScript scripts can run it too.
// Space turns the other way; A / D, the arrow keys or a gamepad stick tilt the cube.
export default class Spin {
    speed = 45;
    yaw = 30;
    presses = 0;
    Start() {
        Debug.Log("spinning " + this.gameObject.name);
    }
    Update(dt) {
        if (Input.GetKeyDown("space")) {
            this.speed = -this.speed;
            this.presses++;
        }
        this.yaw += this.speed * dt;
        const tilt = Input.GetAxis("Horizontal") * 30;
        this.transform.localEulerAngles = new Vector3(25, this.yaw, tilt);
    }
}
